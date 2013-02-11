/* ordon.c
 * Par Guillaume Lahaie
 * LAHG04077707
 * Dernière modification: 6 février 2013
 *
 * Ce programme lit en entrée un fichier contenant des temps d'utilisations 
 * de processus pour un ordonnanceur. Le programme crée 3 processus fils qui
 * ensuite font l'ordonnancement des processus selon différents algorithmes
 * Le premier le fait selon SJF, ensuite le second selon SJFP, et finalement
 * round robin. Ils mettent les informations dans un pipe pour renvoyer au
 * pere, qui affiche les résultats.
 *
 */

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#define TAILLE_T  10 //Taille minimale du tableau de processus
#define MAX_LIG 255
#define IDLE "IDLE"

enum Erreur {ERR_ARG = 1, ERR_QUANTUM, ERR_FICHIER, ERR_PID, ERR_PIPE};

enum Algo {SJF, SJFP, RR};

enum Etat {PRET, ACTIF, BLOQUE, FIN};

typedef struct {
    int tempsActif;
    int pid;
    int arrive;
    int tempsTotalProc;          //Pour SJF
    int* tempsProc;
    int positionTempsProc;
    int tailleTempsProc;        //Pour savoir combien d'éléments dans tempsProc
} Processus;

struct Noeud {
    Processus *p;
    struct Noeud *suivant;
};

typedef struct Noeud Noeud;

typedef struct  {
    Noeud *debut;
    Noeud *fin;
    int taille;
} Liste;

//Lire_fichier: traite le fichier donné en argument et retourne un tableau de Processus
Liste *lireFichier(FILE* fichier);

int compareArrive(Processus *p1, Processus* p2);

void traiterSJF(Liste *l, int *pipe);
void traiterSJFP(Processus *p, int taille, int *writePipe, int quantum);

void traiterAttente(Processus *p, int temps);
int traiterBloque(Processus *p);
enum Etat traiterActif(Processus **p, int *pipe, int temps, int quantum, enum Algo algo);
Processus * choisirProcessus(Liste *pret);
int compareSJF (Processus *p1, Processus *p2);
int comparePid(Processus *p1, Processus *p2);
void libererProcessus(Processus *p);


//--------------------------------------------------------------------------------
// Structures et fonctions pour liste

Liste *listeCreer();
Noeud *listeInserer(Liste *l, Processus *p);
Noeud *listeRetirer(Liste *l, Processus *p);
Noeud *listeChercher(Liste *l,Processus *p, Noeud **prec);
int listeVide(Liste *l);
void listeQuickSort(Liste *l, int (*compare)(Processus *p1, Processus *p2));
Noeud *quickSort(Noeud *n, int (*compare)(Processus *p1, Processus *p2));
Noeud *chercherPivot(Noeud *n);
int listeLongueurPartielle(Noeud *n);

//--------------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    int quantum, readPipe1[2] = {-1, -1}, readPipe2[2] = {-1, -1}, vtfr, tailleP, status, i, j;
    char buf;
    FILE* fichier;
    Processus* p;
    Noeud *n;
    Liste *l;
    //Début: on vérifie les arguments
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <nom_fichier> <quantum>\n", argv[0]);
        exit(ERR_ARG);
    }

    //On vérifie aussi si le quantum est légale (donc > 0)
    if((quantum = atoi(argv[2])) <= 0) {
        fprintf(stderr, "Erreur: quantum illegale. Veuillez entrer un quantum plus grand que 0.\n");
        exit(ERR_QUANTUM);
    }

    //Ouverture du fichier
    if((fichier = fopen(argv[1], "r")) == NULL) {
        fprintf(stderr, "Erreur lors de l'ouverture du fichier.\n");
        exit(ERR_FICHIER);
    }
    l = lireFichier(fichier);
    fclose(fichier);

    if(pipe(readPipe1) < 0) {
        fprintf(stderr, "Erreur de création du pipe.\n");
        exit(ERR_PIPE);
    }

    //On trie la liste en ordre d'arrive des processus
    listeQuickSort(l, compareArrive);
    n = l->debut;
    while(n != NULL) {
        printf("PID %d\n", n->p->pid);
        n = n->suivant;
    }

    vtfr = fork();
    if(vtfr != 0) {
        close(readPipe1[1]);
        while(read(readPipe1[0], &buf, 1) > 0) {
            printf("%c", buf);
        }
        printf("\n");
        close(readPipe1[0]);
        wait(&status);
    } else {
        close(readPipe1[0]);
        traiterSJF(l, readPipe1);
    }
    
/*
    if(pipe(readPipe2) < 0) {
        fprintf(stderr, "Erreur de création du pipe.\n");
        exit(ERR_PIPE);
    }

    //SJF avec Preemption
    vtfr = fork();
    if(vtfr != 0) {
        close(readPipe2[1]);
        while(read(readPipe2[0], &buf, 1) > 0) {
            printf("%c", buf);
        }
        printf("\n");
        close(readPipe2[0]);
        wait(&status);
        exit(EXIT_SUCCESS);
    } else {
        close(readPipe2[0]);
        traiterSJFP(p, tailleP, readPipe2, quantum);
    } */
    return 0;
}

Liste *lireFichier(FILE* fichier) {

    int i, tailleEntMax = TAILLE_T;
    int prochInt, position, neg; //neg: 0 faux, 1 vrai
    char ligne[MAX_LIG], *ptrLig, *e;
    Liste *l = listeCreer();
    Processus *temp;

    while((fgets(ligne, MAX_LIG, fichier)) != NULL) {
        //On insère dans le tableau
        //Debut: le pid
        neg = 0;
        position = 0;
        temp = (Processus *)malloc(sizeof(Processus));
        temp->tempsProc = (int *)malloc(tailleEntMax*sizeof(int));
        temp->positionTempsProc = 0;
        temp->tempsActif = 0;
        for(i = 0,ptrLig = ligne; ;ptrLig = e, i++) {
            prochInt = (int)strtol(ptrLig, &e, 10);
            if(ptrLig == e) {
                break;
            }
            if(i == 0 || i == 1) {//C'est le PID
                if(prochInt <= 0) {
                    fprintf(stderr, "Valeur de PID ou temp d'arrive illégale.\n");
                    exit(ERR_PID);
                }
                if(i == 0) {
                    temp->pid = prochInt;
                }
                else {
                    temp->arrive = prochInt;
                }
            } else { //C'est une info

                if(i >= tailleEntMax) {
                    tailleEntMax *= 2;
                    temp->tempsProc = (int *)realloc(temp->tempsProc, tailleEntMax*sizeof(int));
                }
                if((prochInt < 0 && neg == 0) || (prochInt >0 && neg == 1)) {
                    temp->tempsProc[++position] = prochInt;
                    neg = (neg==0?1:0);
                    temp->tempsTotalProc += (prochInt>0?prochInt:0);
                } else if ((prochInt < 0 && neg == 1) || (prochInt > 0 && neg == 0)) {
                    temp->tempsProc[position] += prochInt;
                    temp->tempsTotalProc += (prochInt>0?prochInt:0);
                }
            }
        }
        assert(i >= 3 && "Manque d'information pour un processus.");
        temp->tailleTempsProc = position+1;
        listeInserer(l, temp);
    }
    return l;
}

int compareArrive(Processus *p1, Processus *p2) {
    return p1->arrive - p2->arrive;
}

void traiterSJF(Liste *l, int *pipe) {

    Liste *pret = listeCreer();
    Liste *bloque = listeCreer();
    Noeud *position = l->debut, *posBloque, *temp; //Dans l
    int taillePret, i, fin, quantum = 1;
    Processus *actif = NULL;
    int temps = 0, idle = 0, debutIdle;
    char chaine[20];
    enum Etat changement;

    while(1) {
        if(position==NULL && listeVide(pret) && listeVide(bloque) && actif == NULL)
            return;
        //On regarde si de nouveaux processus arrive:: on les ajoute à la
        //liste pret
        while(position != NULL && position->p->arrive <= temps) {
            listeInserer(pret, position->p);
            position = position->suivant;
        }
        
        //Maintenant on traite les processus bloques
        posBloque = bloque->debut;
//        if(posBloque)
//            printf("posBloque = PID %d\n", posBloque->p->pid);
        while (posBloque != NULL) {
            //On met à jour le temp de blocage
            if((++posBloque->p->tempsProc[posBloque->p->positionTempsProc]) >= 0) {
                //Plus bloque: termine ou pret?
                if((++posBloque->p->positionTempsProc) >= posBloque->p->tailleTempsProc) {
                    posBloque = listeRetirer(bloque, posBloque->p);
                } else {
                    //Processus passe à prêt
                    listeInserer(pret, posBloque->p);
                    posBloque = listeRetirer(bloque, posBloque->p);
                }
            } else {
                posBloque = posBloque->suivant;
            }
        }

        //Maintenant on traite le processus actif, s'il y en a un

//        printf("Verifie si actif\n");
        if(actif != NULL) {
//            printf("actif n'est pas NULL\n");
            changement = traiterActif(&actif, pipe, temps, quantum, SJF);
            if (changement == BLOQUE) {
//                printf("Processus bloque, on l'insère dans la liste\n");
                listeInserer(bloque, actif);
                actif = NULL;
            } else if (changement == FIN) {
                libererProcessus(actif);
                free(actif);
                actif = NULL;
            }
        } 
        if(actif == NULL) {
//            printf("Actif est NULL\n");
            //On choisit un nouveau processus
            if(listeVide(pret) && !idle) {
                debutIdle = temps;
                idle = 1;
            } else if(listeVide(pret) && idle) {
            } else {
                //On a un processus, donc on imprime le idle avant
                if (idle) {
                    sprintf(chaine, "%s : %d %d\n", IDLE, debutIdle, temps);
                    write(pipe[1], chaine, strlen(chaine));
                    idle = 0;
                }
                actif = choisirProcessus(pret);
//                printf("Retour de choisirProcessus\n");
//                printf("actif = PID %d\n", actif->pid);
                sprintf(chaine, "PID %d : %d ", actif->pid, temps);
                write(pipe[1], chaine, strlen(chaine));
            }
        }
        temps++;

/*        printf("fin de boucle\n");
        printf("temps = %d\n", temps-1);
        temp = position;
        printf("etat des processus nouveau:\n");
        while(temp != NULL) {
            printf("PID %d\n", temp->p->pid);
            temp = temp->suivant;
        }
        temp = pret->debut;
        printf("etat des processus pret:\n");
        while(temp != NULL) {
            printf("PID %d\n", temp->p->pid);
            temp = temp->suivant;
        }
        temp = bloque->debut;
        printf("etat des processus bloque:\n");
        while(temp != NULL) {
            printf("PID %d\n", temp->p->pid);
            temp = temp->suivant;
        }
        printf("etat des processus actifs:\n");
        if(actif) 
            printf("PID %d\n", actif->pid);
        else
            printf("Processeur est IDLE\n");
        printf("-----------------------------\n");*/

       /* if(fin || temps > 30) {
            printf("On fait break : temps = %d\n", temps);
            if(!position)
                printf("position est NULL\n");
            printf("taille de pret: %d\n", longueurListePartielle(pret->debut));
            printf("taille de bloque: %d\n", longueurListePartielle(bloque->debut));
            break;
        } */
    }
}
/*
void traiterAttente(Processus *p, int temps) {
    if(p->arrive <= temps) {
        p->etat = PRET;
    }
}


//Retourne 1 si le processus est pret, pour indiquer qu'il faut refaire
//la sélection, 0 dans tout autre cas.
int traiterBloque(Processus *p) {
    //printf("PID %d : bloque à %d\n", p->pid, p->tempsProc[p->positionTempsProc]);
    if((++(p->tempsProc[p->positionTempsProc])) >= 0) {
        if((++(p->positionTempsProc)) >= p->tailleTempsProc) {
            p->etat = FIN;
        } else {
            //printf("processus PRET\n");
            p->etat = PRET;
            return 1;
        }
    }
    return 0;
}

*/
//Retourne 1 si pret, 0 si bloque ou fin, -1 si aucun des cas
enum Etat traiterActif(Processus **p, int *pipe, int temps, int quantum, enum Algo algo) {


    char chaine[10];
    (*p)->tempsTotalProc--;
    (*p)->tempsActif++;
    if((--((*p)->tempsProc[(*p)->positionTempsProc])) <= 0) {
        sprintf(chaine, "- %d\n", temps);
        write(pipe[1], chaine, strlen(chaine));
        if((++((*p)->positionTempsProc)) >= (*p)->tailleTempsProc) {
            return FIN;
        } else {
            return BLOQUE;
        }
        return ACTIF;
    } /*else if (algo == SJFP && (*p)->tempsActif >= quantum) {
        sprintf(chaine, "%d\n", temps);
        write(pipe[1], chaine, strlen(chaine));
        (*p)->tempsActif = 0;
        return 1;
    } */ else {
        return -1;
    }
}

void libererProcessus(Processus *p) {
    free(p->tempsProc);
}

Processus * choisirProcessus(Liste *pret) {

    //printf("Appel de choisirProcessus\n");
    Processus *p;
    
    //Premierement, on classe les processus en ordre de temps de processus restant
    
    listeQuickSort(pret, compareSJF);

    //On prends le premier élément de la liste
    p = pret->debut->p;
    //printf("p = PID %d\n", p->pid);
    listeRetirer(pret, pret->debut->p);
    return p;
}

int compareSJF(Processus* p1, Processus* p2) {
    return p1->tempsTotalProc - p2->tempsTotalProc;
}

/*
//-----------------------------------------------------------------------------
//SJF avec preemption


void traiterSJFP(Processus *p, int taille, int *writePipe, int quantum) {

    Processus **pret = (Processus **)malloc(taille*sizeof(Processus *));
    int taillePret, i, fin;
    Processus *actif = NULL;
    int temps = 0, idle = 0, debutIdle, temp, debloque;
    char chaine[20];

    while(1) {
        debloque = 0;
        taillePret = 0;
        fin = 1;
        actif = NULL;
        for(i = 0; i < taille; i++) {
            if(p[i].etat == ATTENTE) {
                traiterAttente(&p[i], temps);
                fin = 0;
            }
            if(p[i].etat == BLOQUE) {
                debloque = traiterBloque(&p[i]);
                fin = 0;
            }
            if(p[i].etat == PRET) {
                pret[taillePret++] = &p[i];
                fin = 0;
            }
            if(p[i].etat ==ACTIF) {
                actif = &p[i];
                fin = 0;
            }
        }

        if(actif != NULL) {
           
            switch(traiterActif(&actif, writePipe, temps, quantum, SJFP)) {
                case 1: pret[taillePret++] = actif;
                case 0: actif = NULL;
                        break;
            }
        } 
        if(debloque && actif != NULL) {
            //On doit voir si le programme debloque est plus court, donc on change
            //d'etat
            pret[taillePret++] = actif;
            actif = NULL;
        }
        if(actif == NULL) {
            //On choisit un nouveau processus
            if(taillePret == 0 && !idle) {
                debutIdle = temps;
                idle = 1;
            } else if(taillePret == 0 && idle) {
            } else {

                //On a un processus, donc on imprime le idle avant
                if (idle) {
                    sprintf(chaine, "%s : %d %d\n", IDLE, debutIdle, temps);
                    write(writePipe[1], chaine, strlen(chaine));
                    idle = 0;
                }
                actif = choisirProcessus(pret,taillePret);
                sprintf(chaine, "PID %d : %d ", actif->pid, temps);
                write(writePipe[1], chaine, strlen(chaine));
            }
        }
        temps++;
        if(fin) {
            break;
        }
    }
}
*/
//--------------------------------------------------------------------------------

Liste *listeCreer(void) {

    Liste *l = (Liste *)malloc(sizeof(Liste));
    l->debut = NULL;
    l->fin = NULL;
    l->taille = 0;
    return l;
}

//Pour l'utilisation de la liste que je fais, les insertions sont toujours
//à la fin de la liste
Noeud *listeInserer(Liste *l, Processus *p) {
    Noeud *temp = (Noeud*)malloc(sizeof(Noeud));
    temp->p = p;
    temp->suivant = NULL;
    if(l->debut == NULL) {
        l->debut = temp;
        l->fin = temp;
        l->taille++;
        return temp;
    } else {
        //On ajuste la fin de la liste
        l->fin->suivant = temp;
        l->taille++;
        return temp;
    }
}

//Cas à traiter: debut de la liste, fin de la liste et milieu
Noeud *listeRetirer(Liste *l, Processus *p) {
    assert(l->taille > 0 && "Ne peut retirer d'une liste vide.");
    Noeud *del = NULL;
    Noeud *prec = NULL;
    Noeud *temp;

   // printf("Appel de listeRetirer\n");
    del = listeChercher(l, p, &prec);
/*    if(del != NULL)
        printf("del = PID %d\n", del->p->pid);
    else
        printf("del est NULL\n");
    if(prec != NULL)
        printf("prec = PID %d\n", prec->p->pid);
    else
        printf("prec est NULL\n");
*/
    if(del != NULL) {
        l->taille--;
        //Cas du dernier élément de la liste
        if(l->taille == 0) {
//            printf("Liste est maintenant vide\n");
            l->debut = NULL;
            l->fin = NULL;
            return NULL;
        }
        //Cas du premier item de la liste
        if(l->debut == del) {
//            printf("retire debut de la liste\n");
            l->debut = del->suivant;
            return l->debut;
        }
        //Cas du dernier item
        if(l->fin == del) {
//            printf("retire fin de la liste\n");
            prec->suivant = NULL;
            return NULL;
        }
        temp = del;
        prec->suivant = del->suivant;
        return temp->suivant;
    } else {
        return NULL;
    }
}

Noeud *listeChercher(Liste *l,Processus *p, Noeud **prec) {
    assert(l->taille > 0 && "Ne peut chercher dans une liste vide.");
    Noeud *ptr = l->debut;
    Noeud *tmp = NULL;
    int trouve = 0;

/*    printf("Appel de listeChercher\n");
    printf("On cherche PID %d\n", p->pid);
*/
    while(ptr != NULL) {
        if(!comparePid(ptr->p, p)) {
            trouve = 1;
            break;
        } else {
            tmp = ptr;
            ptr = ptr->suivant;
        }
    }
    if(trouve) {
        if(prec) {
            *prec = tmp;
        }
//        printf("trouve\n");
//        if(ptr) printf("ptr->pid = %d\n", ptr->p->pid);
        return ptr;
    } else {
        return NULL;
    }
}

int listeVide(Liste *l) {
    if(l->taille == 0)
        return 1;
    return 0;
}

int longueurListePartielle(Noeud *n) {
    Noeud *temp = n;
    int total = 0;
    while (temp != NULL) {
        total++;
        temp = temp->suivant;
    }
    return total;
}

Noeud *choisirPivot(Noeud *n) {
    int k, l, i = 0;
    l = longueurListePartielle(n);
    Noeud *pivot=n;
    k=rand()%l;  
    for (; i < k; ++i) {
        pivot=pivot->suivant;
    }
    return pivot;
}

Noeud *quickSort(Noeud *n, int (*compare)(Processus *p1, Processus *p2)) {

    // Return NULL list
    if (longueurListePartielle(n) <= 1) 
        return n;
    Noeud *petit=NULL, *grand=NULL, *proch, *fin, *temp=NULL;

    // Choisir un pivot
    Noeud *pivot = choisirPivot(n);

    while(n != NULL) {
        proch = n->suivant;

        if((compare)(n->p, pivot->p)) {
            n->suivant=temp;
            temp = n;
        }
        n = proch;
    }

    while(temp != NULL) {
        proch = temp->suivant;
        if((compare)(temp->p, pivot->p) < 0) {
            temp->suivant = petit;
            petit = temp;
        } else {
            temp->suivant = grand;
            grand = temp;    
        }
        temp = proch;
    }

    petit = quickSort(petit, (*compare));
    grand = quickSort(grand, (*compare));

    if(petit != NULL) {
        fin = petit;
        while(fin->suivant != NULL){
            fin=fin->suivant;
        }
        pivot->suivant=grand;
        fin->suivant = pivot;
        return petit;        
    } else {
        pivot->suivant = grand;
        return pivot;   
    }
}

void listeQuickSort(Liste *l, int (*compare)(Processus *p1, Processus *p2)) {


    Noeud *temp1, *temp2;

//   printf("Appel de listeQuickSort\n");    
    if(l->taille > 1) {
        l->debut = quickSort(l->debut, (*compare));

        //Maintenant on replace l->fin
        temp1 = l->debut;
        temp2 = NULL;

    
        while(temp1 != NULL) {
            temp2 = temp1;
            temp1 = temp1->suivant;
        }

        l->fin = temp2;
    }

//    printf("fin de quickSort: etat de la liste\n");
//    printf("l->debut = PID %d\n", l->debut->p->pid);
//    printf("l->fin = PID %d\n", l->fin->p->pid);
    return;
}

//Deux processus sont pareils si ils ont le meme pid
int comparePid(Processus *p1, Processus *p2) {
    return p1->pid - p2->pid;
}

