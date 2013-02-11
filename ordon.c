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


//Structures pour les listes
struct Noeud {
    void *contenu;
    struct Noeud *suivant;
};

typedef struct Noeud Noeud;

typedef struct  {
    Noeud *debut;
    Noeud *fin;
    int taille;
} Liste;


typedef struct {
    int ligneFich;              //Pour départager deux fichiers egaux
    int tempsActif;
    int pid;
    int arrive;
    int tempsTotalProc;          //Pour SJF
    Liste *tempsProc;
    Noeud *posTempsProc;
} Processus;

//Lire_fichier: traite le fichier donné en argument et retourne un tableau de Processus
Liste *lireFichier(FILE* fichier);


//Comparateurs différents pour listes
int compareTempsProc(const void *p1, const void *p2);
int compareArrive(const void *p1, const void *p2);
int compareSJF (const void *p1, const void *p2);
int compareProcessus(const void *p1, const void *p2);
void traiterSJF(Liste *l, int *pipe);
void traiterSJFP(Liste *l, int *pipe, int quantum);

void traiterAttente(Processus *p, int temps);
int traiterBloque(Processus *p);
enum Etat traiterActif(Processus **p, int *pipe, int temps, int quantum, enum Algo algo);
Processus * choisirProcessus(Liste *pret);
void libererProcessus(Processus *p);


//--------------------------------------------------------------------------------
// Structures et fonctions pour liste

Liste *listeCreer();
Noeud *listeInserer(Liste *l, void *p);
Noeud *listeRetirer(Liste *l, void *p, int (*compare)(const void *p1, const void *p2));
Noeud *listeChercher(Liste *l,void *p, int (*compare)(const void *p1, const void *p2), Noeud **prec);
int listeVide(Liste *l);
void listeQuickSort(Liste *l, int (*compare)(const void *p1, const void *p2));
Noeud *quickSort(Noeud *n, int (*compare)(const void *p1,const void *p2));
Noeud *chercherPivot(Noeud *n);
int listeLongueurPartielle(Noeud *n);

//--------------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    int quantum, readPipe1[2] = {-1, -1}, readPipe2[2] = {-1, -1}, vtfr, status;
    Noeud *tempsProc;
    char buf;
    FILE* fichier;
    Noeud *n, *suivant;
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
        printf("PID %d -- ", ((Processus *)n->contenu)->pid);
        tempsProc = ((Processus *)n->contenu)->tempsProc->debut;
        while(tempsProc != NULL) {
            printf("%d ", *((int *)tempsProc->contenu));
            tempsProc = tempsProc->suivant;
        }
        printf("\n");
        n = n->suivant;
       
    }

/*        traiterSJF(l, readPipe1);

    n = l->debut;
    suivant = n->suivant;
    while(n != NULL) {
        libererProcessus((Processus *)(n->contenu));
        free(n->contenu);
        free(n);
        n = suivant;
        if(n)
            suivant = n->suivant;
    }
    free(l);
*/
/*
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
*/
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
        traiterSJFP(l, readPipe2, quantum);
    } 
    return 0;
}

Liste *lireFichier(FILE* fichier) {

    int i, *intListe, noLig = 0;
    int prochInt, neg; //neg: 0 faux, 1 vrai
    char ligne[MAX_LIG], *ptrLig, *e;
    Liste *l = listeCreer();
    Processus *temp;

    while((fgets(ligne, MAX_LIG, fichier)) != NULL) {
        //On insère dans le tableau
        //Debut: le pid
        neg = 1;
        temp = (Processus *)malloc(sizeof(Processus));
        temp->tempsProc = listeCreer();
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

                if((prochInt < 0 && neg == 0) || (prochInt >= 0 && neg == 1)) {
                    intListe = (int*)malloc(sizeof(int));
                    *intListe = prochInt;
                    listeInserer(temp->tempsProc, intListe);
                    neg = (neg==0?1:0);
                    temp->tempsTotalProc += (prochInt>0?prochInt:0);
                } else if ((prochInt < 0 && neg == 1) || (prochInt >= 0 && neg == 0)) {
                    *((int *)temp->tempsProc->fin->contenu) += prochInt;
                    temp->tempsTotalProc += (prochInt>0?prochInt:0);
                }
            }
        }
        temp->ligneFich = ++noLig;
        temp->posTempsProc = temp->tempsProc->debut;
        assert(i >= 3 && "Manque d'information pour un processus.");
        listeInserer(l, temp);
    }
    return l;
}

int compareArrive(const void *p1, const void *p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    int temp = pr1->arrive - pr2->arrive;
    if(temp != 0) 
        return temp;
        
    return pr1->ligneFich - pr2->ligneFich;
}

int compareTempsProc (const void *p1, const void *p2) {
    int temps1 = *((int *)p1);
    int temps2 = *((int *)p2);
    return temps1 - temps2;
}

void traiterSJF(Liste *l, int *pipe) {

    Liste *pret = listeCreer();
    Liste *bloque = listeCreer();
    Noeud *position = l->debut, *posBloque, *temp; //Dans l
    int quantum = 1;
    Processus *actif = NULL;
    int temps = 0, idle = 0, debutIdle;
    char chaine[20];
    enum Etat changement;

    while(1) {
        if(position==NULL && listeVide(pret) && listeVide(bloque) && actif == NULL) {
            printf("Fin de SJF: on fait des free\n");
            free(pret);
            free(bloque);
            return;
        }
        //On regarde si de nouveaux processus arrive:: on les ajoute à la
        //liste pret
        while(position != NULL && ((Processus *)(position->contenu))->arrive <= temps) {
            listeInserer(pret, (void *)position->contenu);
            position = position->suivant;
        }
        
        //Maintenant on traite les processus bloques
        posBloque = bloque->debut;
//        if(posBloque)
//            printf("posBloque = PID %d\n", posBloque->p->pid);
        while (posBloque != NULL) {
            //On met à jour le temp de blocage
            ++(*((int *)((Processus *)posBloque->contenu)->posTempsProc->contenu));
            if((*((int *)((Processus *)posBloque->contenu)->posTempsProc->contenu)) >= 0) {
                //Plus bloque: termine ou pret?
                ((Processus *)posBloque->contenu)->posTempsProc = ((Processus *)posBloque->contenu)->posTempsProc->suivant;
                if(((Processus *)posBloque->contenu)->posTempsProc == NULL) {
                    posBloque = listeRetirer(bloque, posBloque->contenu, compareProcessus);
                } else {
                    //Processus passe à prêt
                    listeInserer(pret, posBloque->contenu);
                    posBloque = listeRetirer(bloque, posBloque->contenu, compareProcessus);
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
                listeInserer(bloque, (void *)actif);
                actif = NULL;
            } else if (changement == FIN) {
                //A revoir
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
                    sprintf(chaine, "%s : %d-%d\n", IDLE, debutIdle, temps);
                    write(pipe[1], chaine, strlen(chaine));
                    idle = 0;
                }
                actif = choisirProcessus(pret);
//                printf("Retour de choisirProcessus\n");
//                printf("actif = PID %d\n", actif->pid);
                sprintf(chaine, "PID %d : %d", actif->pid, temps);
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


//Retourne 1 si pret, 0 si bloque ou fin, -1 si aucun des cas
enum Etat traiterActif(Processus **p, int *pipe, int temps, int quantum, enum Algo algo) {

    char chaine[10];
    (*p)->tempsTotalProc--;
    (*p)->tempsActif++;
    (--(*(int *)((*p)->posTempsProc->contenu)));
    if((*(int *)((*p)->posTempsProc->contenu)) <= 0) {
        sprintf(chaine, "-%d\n", temps);
        write(pipe[1], chaine, strlen(chaine));
        //On pointe vers le prochain element de la liste
        (*p)->posTempsProc = (*p)->posTempsProc->suivant;
        if((*p)->posTempsProc == NULL) {
            return FIN;
        } else {
            return BLOQUE;
        }
        return ACTIF;
    } 
    if (algo == SJFP) {
       if( (*p)->tempsActif >= quantum) {
        sprintf(chaine, "%d\n", temps);
        write(pipe[1], chaine, strlen(chaine));
        (*p)->tempsActif = 0;
        return PRET;
        }
    }
    return ACTIF;
}

void libererProcessus(Processus *p) {
    Noeud *n = p->tempsProc->debut;
    Noeud *suivant = n->suivant;

    //printf("PID %d liberer\n", p->pid);
    //printf("debut de liste: %d\n", *((int*)p->tempsProc->debut->contenu));
    //if(suivant == NULL)
    //    printf("suivant est NULL\n");
    while (n != NULL) {
    //    printf("tempsProc : %d\n", *((int*)n->contenu));
        free(n->contenu);
        free(n);
        n = suivant;
        if(n)
            suivant = n->suivant;
    }
    free(p->tempsProc);
}

Processus * choisirProcessus(Liste *pret) {

    //printf("Appel de choisirProcessus\n");
    Processus *p = (Processus *)pret->debut->contenu;
    Noeud *iterateur = pret->debut->suivant;
    
    //Premierement, on classe les processus en ordre de temps de processus restant
    
//    printf("Pret est\n");
    iterateur = pret->debut;
//    while(iterateur) {
//        printf("PID %d\n", ((Processus *)iterateur->contenu)->pid);
//        if(iterateur->suivant)
//            printf("iterateur->suivant est non NULL\n");
//        else
//            printf("iterateur->suivant est NULL\n");
//        iterateur = iterateur->suivant;
//    }
//    printf("p = PID %d\n", p->pid);
//    if(iterateur != NULL)
//        printf("iterateur = PID %d\n",((Processus *)iterateur->contenu)->pid);
//    else
//        printf("iterateur est NULL\n");
    while(iterateur != NULL) {
        if(compareSJF((const void*)p, iterateur->contenu) < 0) {
            p = (Processus *)iterateur->contenu;
        }
        iterateur = iterateur->suivant;
    }
    //On prends le premier élément de la liste
    //printf("p = PID %d\n", p->pid);
    listeRetirer(pret, p, compareProcessus);
    return p;
}

int compareSJF(const void* p1, const void* p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    int temp = pr1->tempsTotalProc - pr2->tempsTotalProc;
    if(temp != 0)
        return temp;
    else 
        return pr1->ligneFich - pr2->ligneFich;
}
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
//
//voir le warning
Noeud *listeInserer(Liste *l, void *p) {
//    printf("Appel de listeInserer\n");
    Noeud *temp = (Noeud*)malloc(sizeof(Noeud));
    temp->contenu = p;
    temp->suivant = NULL;
    if(l->debut == NULL) {
//        printf("Ajoute debut liste.\n");
        //Liste vide
        l->debut = temp;
        l->fin = temp;
        l->taille++;
    } else {
//        printf("Ajoute fin liste.\n");
        //On ajuste la fin de la liste
        l->fin->suivant = temp;
        l->fin = temp;
        l->taille++;
    }
    return temp;
}

//Cas à traiter: debut de la liste, fin de la liste et milieu
//
//Ici, comme lorsqu'on insere dans un
Noeud *listeRetirer(Liste *l, void *p, int (*compare)(const void *p1, const void *p2)) {
    assert(l->taille > 0 && "Ne peut retirer d'une liste vide.");
    Noeud *del = NULL;
    Noeud *prec = NULL;
    Noeud *temp;

   // printf("Appel de listeRetirer\n");
    del = listeChercher(l, p, (*compare), &prec);
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

        //La recheche est ok
        l->taille--;
        //Cas du dernier élément de la liste
        if(l->taille == 0) {
//            printf("Liste est maintenant vide\n");
            l->debut = NULL;
            l->fin = NULL;
        } else if((*compare)(l->debut->contenu, del->contenu) == 0) {
//            printf("retire debut de la liste\n");
            l->debut = del->suivant;
            return l->debut;
        } else if((*compare)(l->fin->contenu, del->contenu) == 0) {
//            printf("retire fin de la liste\n");
            prec->suivant = NULL;
            l->fin = prec;
        } else {
            if(prec == NULL) 
//                printf("prec est NULL\n");
            if(del == NULL) 
//                printf("del est NULL\n");
            temp = del;
            prec->suivant = del->suivant;
        }
    }
    if(del == NULL || l->taille == 0 || l->fin == del)
        return NULL;
    else
        return temp->suivant;
}

Noeud *listeChercher(Liste *l, void *p, int (*compare)(const void *p1, const void *p2), Noeud **prec) {
    assert(l->taille > 0 && "Ne peut chercher dans une liste vide.");
    Noeud *ptr = l->debut;
    Noeud *tmp = NULL;
    int trouve = 0;

/*    printf("Appel de listeChercher\n");
    printf("On cherche PID %d\n", p->pid);
*/
    while(ptr != NULL) {
        if(!(*compare)(ptr->contenu, p)) {
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

Noeud *quickSort(Noeud *n, int (*compare)(const void *p1, const void *p2)) {

    // Return NULL list
    if (longueurListePartielle(n) <= 1) 
        return n;
    Noeud *petit=NULL, *grand=NULL, *proch, *fin, *temp=NULL;

    // Choisir un pivot
    Noeud *pivot = choisirPivot(n);

    while(n != NULL) {
        proch = n->suivant;

        if((compare)(n->contenu, pivot->contenu)) {
            n->suivant=temp;
            temp = n;
        }
        n = proch;
    }

    while(temp != NULL) {
        proch = temp->suivant;
        if((compare)(temp->contenu, pivot->contenu) < 0) {
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

void listeQuickSort(Liste *l, int (*compare)(const void *p1, const void *p2)) {


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

//Deux processus sont pareils si ils ont le meme pid et le meme temps d'arrive
int compareProcessus(const void *p1, const void *p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    int temp = pr1->pid - pr2->pid;
    if (temp != 0)
        return temp;
    else
        return pr1->arrive - pr2->arrive;
}


void traiterSJFP(Liste *l, int *pipe, int quantum) {

    Liste *pret = listeCreer();
    Liste *bloque = listeCreer();
    Noeud *position = l->debut, *posBloque, *temp; //Dans l
    Processus *actif = NULL;
    int temps = 0, idle = 0, debutIdle, verifProc;
    char chaine[20];
    enum Etat changement;

    while(1) {
        if(position==NULL && listeVide(pret) && listeVide(bloque) && actif == NULL) {
//            printf("Fin de SJFP: on fait des free\n");
            free(pret);
            free(bloque);
            return;
        }

        verifProc = 0;
        //On regarde si de nouveaux processus arrive:: on les ajoute à la
        //liste pret
        while(position != NULL && ((Processus *)(position->contenu))->arrive <= temps) {
            verifProc = 1;
//            printf("Appel de listeInserer sur pret de position, temps = %d\n", temps);
            listeInserer(pret, (void *)position->contenu);
            position = position->suivant;
        }
        
        //Maintenant on traite les processus bloques
        posBloque = bloque->debut;
//        if(posBloque)
//            printf("posBloque = PID %d\n", posBloque->p->pid);
        while (posBloque != NULL) {
            //On met à jour le temp de blocage
            ++(*((int *)((Processus *)posBloque->contenu)->posTempsProc->contenu));
            if((*((int *)((Processus *)posBloque->contenu)->posTempsProc->contenu)) >= 0) {
                //Plus bloque: termine ou pret?
                ((Processus *)posBloque->contenu)->posTempsProc = ((Processus *)posBloque->contenu)->posTempsProc->suivant;
                if(((Processus *)posBloque->contenu)->posTempsProc == NULL) {
                    posBloque = listeRetirer(bloque, posBloque->contenu, compareProcessus);
                } else {
                    verifProc = 1;
                    //Processus passe à prêt

//                    printf("Appel de listeInserer sur pret de bloque, temps = %d\n", temps);
                    listeInserer(pret, posBloque->contenu);
                    posBloque = listeRetirer(bloque, posBloque->contenu, compareProcessus);
                }
            } else {
                posBloque = posBloque->suivant;
            }
        }

        //Maintenant on traite le processus actif, s'il y en a un

//        printf("Verifie si actif\n");
        if(actif != NULL) {
//            printf("actif n'est pas NULL\n");
            changement = traiterActif(&actif, pipe, temps, quantum, SJFP);
            if (changement == BLOQUE) {

//                printf("Appel de listeInserer sur bloque de actif, temps = %d\n", temps);
                listeInserer(bloque, (void *)actif);
                actif = NULL;
            } else if (changement == FIN) {
                //A revoir
                libererProcessus(actif);
                free(actif);
                actif = NULL;
            } else if (changement == PRET) {
//                printf("Appel de listeInserer sur pret de actif, temps = %d\n", temps);
                listeInserer(pret, (void *)actif);
                actif = NULL;
            }
        } 
        if(verifProc == 1 && actif != NULL) {
//            printf("Appel de listeInserer sur pret de verifProc, temps = %d\n", temps);
    
            listeInserer(pret, (void *)actif);
            actif = NULL;
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
                    sprintf(chaine, "%s : %d-%d\n", IDLE, debutIdle, temps);
                    write(pipe[1], chaine, strlen(chaine));
                    idle = 0;
                }
                actif = choisirProcessus(pret);
//                printf("Retour de choisirProcessus\n");
//                printf("actif = PID %d\n", actif->pid);
                sprintf(chaine, "PID %d : %d", actif->pid, temps);
                write(pipe[1], chaine, strlen(chaine));
            }
        }
        temps++;
    } //fin while
    return;
} //fin traiter SJFP
