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
#include <ctype.h>
#define TAILLE_T  25 //Taille minimale du tableau de processus
#define MAX_LIG 255
#define IDLE "IDLE"
#define SJF_S "SJF"
#define SJFP_S "SJFP"
#define RR_S "RR"

enum Erreur {ERR_ARG = 1, ERR_QUANTUM, ERR_FICHIER, ERR_PID, ERR_PIPE};

enum Algorithme {SJF, SJFP, RR};

enum Etat {PRET, ACTIF, BLOQUE, FIN, FIN_QUANTUM};

enum Ecriture {LIGNE, TEXTE};


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
    enum Etat etat;
    int tempsDebut;
    int tempsFin;
    int ligneFich;              //Pour départager deux processus
    int tempsActif;
    int pid;
    int arrive;
    Liste *tempsProc;
} Processus;

//Lire_fichier: traite le fichier donné en argument et retourne un tableau de Processus
Liste *lireFichier(FILE* fichier);



//Comparateurs différents pour listes
int compareTempsProc(const void *p1, const void *p2);
int compareArrive(const void *p1, const void *p2);
int compareSJF (const void *p1, const void *p2);
int compareProcessus(const void *p1, const void *p2);

enum Etat traiterActif(Processus *p, int temps,int quantum, enum Algorithme algo);
Processus * choisirProcessus(Liste *pret, enum Algorithme algo);
void libererProcessus(Processus *p);
int traiterListeBloque (Liste *bloque, Liste *pret, int (*compare)(const void *p1, const void *p2));

//--------------------------------------------------------------------------------
// Structures et fonctions pour liste

Liste *listeCreer();
Noeud *listeInserer(Liste *l, void *p);
Noeud *listeRetirer(Liste *l, void *p, int (*compare)(const void *p1, const void *p2));
Noeud *listeChercher(Liste *l,void *p, int (*compare)(const void *p1, const void *p2), Noeud **prec);
Noeud *listeRetirerDebut(Liste *l);
int listeVide(Liste *l);
void listeQuickSort(Liste *l, int (*compare)(const void *p1, const void *p2));
Noeud *quickSort(Noeud *n, int (*compare)(const void *p1,const void *p2));
Noeud *chercherPivot(Noeud *n);
int listeLongueurPartielle(Noeud *n);
char * ajouterCar(int prochCar, char *chaine, int *position, int *tailleTotale, enum Ecriture type);
char * lirePipe(int *pipefd, int vtfr);

void traiterProcessus(Liste *l, int *pipefd, int quantum, enum Algorithme algo);


//--------------------------------------------------------------------------------

int main(int argc, char *argv[]) {

    int quantum, pipefd[3][2], i, vtfr[3];
    char *resultat[3];
    char *e;
    FILE* fichier;
    Noeud *n, *suivant;
    Liste *l;
    //Début: on vérifie les arguments
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <nom_fichier> <quantum>\n", argv[0]);
        exit(ERR_ARG);
    }

    //On vérifie aussi si le quantum est légale (donc > 0)
    quantum = strtol(argv[2], &e, 10);
    if(quantum <= 0 || *e != '\0') {
        fprintf(stderr, "Erreur: quantum illegale. Veuillez entrer un quantum "
                "plus grand que 0.\n");
        exit(ERR_QUANTUM);
    }

    //Ouverture  et lecture du fichier
    if((fichier = fopen(argv[1], "r")) == NULL) {
        fprintf(stderr, "Erreur lors de l'ouverture du fichier.\n");
        exit(ERR_FICHIER);
    }
    l = lireFichier(fichier);
    fclose(fichier);

    //On trie la liste en ordre d'arrive des processus
    listeQuickSort(l, compareArrive);

    //On traite les algorithmes
    for(i = 0; i < 3; i++) {
        if(pipe(pipefd[i]) < 0) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
        if((vtfr[i] = fork()) < 0) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        if(vtfr[i] != 0) {
            resultat[i] = lirePipe(pipefd[i], vtfr[i]);
        } else {
            close(pipefd[i][0]);
            switch(i) {
                case 0: traiterProcessus(l, pipefd[i], quantum, SJF);
                        exit(EXIT_SUCCESS);
                        break;
                case 1: traiterProcessus(l, pipefd[i], quantum, SJFP);
                        exit(EXIT_SUCCESS);
                        break;
                case 2: traiterProcessus(l, pipefd[i], quantum, RR);
                        exit(EXIT_SUCCESS);
                        break;
            }
        }
    }

    //On affiche les résultats
    for(i = 0; i < 3; i++) {
        printf("%s\n", resultat[i]);
        free(resultat[i]);
    }

    //Liberation de l'espace utilisé
    n = l->debut;

    while(n != NULL) {
        libererProcessus((Processus *)n->contenu);
        suivant = n->suivant;
        free(n);
        n = suivant;
    }
    free(l);
 
    return 0;
}

char *lirePipe(int *pipefd, int vtfr) {
    int tailleMax = TAILLE_T;
    int taille = 0, status;
    char *chaine = (char*)malloc(tailleMax);
    int buf;
 
    close(pipefd[1]);
    while(read(pipefd[0], &buf, 1) > 0) {
        chaine = ajouterCar((char)buf, chaine, &taille, &tailleMax, TEXTE);
    }
    chaine = ajouterCar('\0', chaine, &taille, &tailleMax, TEXTE);
    close(pipefd[0]);
    waitpid(vtfr, &status, 0);

    return chaine;
}

char * ajouterCar(int prochCar, char *chaine, int *position, int *tailleMax, 
        enum Ecriture type) {
    char *temp = NULL;

    if((*position) >= (*tailleMax)) {
        (*tailleMax) = (*tailleMax) * 2;
        temp = (char *)realloc(chaine, *tailleMax);
        if(temp) {
            chaine = temp;
        }
        else {
            fprintf(stderr, "erreur de realloc\n");
            exit(EXIT_FAILURE);
        }
    }
    if(type == LIGNE) {
        chaine[(*position)++] = (char)((prochCar=='\n' || prochCar == EOF)?'\0':prochCar);
    } else {
        chaine[(*position)++] = (char)prochCar;
    }
    return chaine;
}


Liste *lireFichier(FILE* fichier) {

    int i, *intListe, noLig = 0, prochCar;
    int prochInt, neg; //neg: 0 faux, 1 vrai
    char *ligne = (char *)malloc(TAILLE_T*sizeof(char)), *ptrLig, *e;
    int longueurMaxLigne = TAILLE_T, longueurLigne = 0;
    Liste *l = listeCreer();
    Processus *temp;

    while((prochCar = fgetc(fichier)) != EOF) {
  
        //On lit tous les chars jusqu'à un différent d'un espace
        while(isspace(prochCar)) {
            prochCar = fgetc(fichier);
        }
        //Ici, vérifier si fin du fichier
        if(prochCar == EOF) {
            if(ligne) {
                free(ligne);
            }
            return l;
        } else {
            //On garde la même ligne qu'auparavant
            longueurLigne = 0;
            //On rempli la ligne:: peut on avoir EOF à l'intérieur?
            while(1) {
                ligne = ajouterCar(prochCar, ligne, &longueurLigne, &longueurMaxLigne, LIGNE);
                if(prochCar == '\n' || prochCar == EOF)
                    break;
                prochCar = fgetc(fichier);
            }
        }

        //On a une ligne de forme string bien formée, on peut l'utilisée de cette façon.
        neg = 1;
        temp = (Processus *)malloc(sizeof(Processus));
        temp->tempsProc = listeCreer();
        temp->tempsActif = 0;

        //On interprète la ligne
        for(i = 0, ptrLig = ligne; ;ptrLig = e, i++) {
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
            } else { //C'est un temps de processus

                if((prochInt < 0 && neg == 0) || (prochInt >= 0 && neg == 1)) {
                    intListe = (int*)malloc(sizeof(int));
                    *intListe = prochInt;
                    listeInserer(temp->tempsProc, intListe);
                    neg = (neg==0?1:0);
                } else if ((prochInt < 0 && neg == 1) || (prochInt >= 0 && neg == 0)) {
                    *((int *)temp->tempsProc->fin->contenu) += prochInt;
                }
            }
        }
        temp->ligneFich = ++noLig;
        assert(i >= 3 && "Manque d'information pour un processus.");
        listeInserer(l, temp);
    }
    if(ligne)
        free(ligne);
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


void traiterProcessus(Liste *l, int *pipe, int quantum, enum Algorithme algo) {

    Liste *pret = listeCreer();
    Liste *bloque = listeCreer();
    Noeud *position = l->debut, *temp;
    Processus *actif = NULL, *ancienActif;
    int temps = 0, idle = 0, debutIdle, preemption;
    char chaine[20];               
    enum Etat changement;

    char blah[] = "Resultat du processus pour: ";
    write(pipe[1], blah, strlen(blah));
    switch(algo) {
        case SJF:   write(pipe[1], SJF_S, strlen(SJF_S));
                    break;
        case SJFP:  write(pipe[1], SJFP_S, strlen(SJFP_S));
                    break;
        case RR:    sprintf(chaine, "%s %d", RR_S, quantum);
                    write(pipe[1], chaine, strlen(chaine));
                    break;
    }
    write(pipe[1], "\n", 1);


    while(position!=NULL || !listeVide(pret) || 
            !listeVide(bloque) || actif != NULL) {

        ancienActif = NULL;
        preemption = 0;
        //On regarde si de nouveaux processus arrive:: on les ajoute à la
        //liste pret
        while(position != NULL && ((Processus *)(position->contenu))->arrive <= temps) {
            if(algo == SJFP)
            preemption = 1;
            listeInserer(pret, (void *)position->contenu);
            position = position->suivant;
        }
        
        //Maintenant on traite les processus bloques

        preemption += traiterListeBloque(bloque, pret, compareProcessus);

        //Maintenant on traite le processus actif, s'il y en a un
        if(actif != NULL) {
            changement = traiterActif(actif, temps, quantum, algo);
            if (changement == BLOQUE) {
                listeInserer(bloque, (void *)actif);
                ancienActif = actif;
                actif = NULL;
            } else if (changement == FIN) {
                //A revoir
                ancienActif = actif;
                actif = NULL;
            } else if (changement == PRET) {
                listeInserer(pret, (void*)actif);
                ancienActif = actif;
                actif = NULL;
            }
            if (algo == SJFP && preemption) {
                listeInserer(pret, (void *)actif);
                ancienActif = actif;
                actif = NULL;
            }
        }

        if(actif == NULL) {
            if(listeVide(pret) && !idle) {
                debutIdle = temps;
                idle = 1;

                if(ancienActif) {
                    sprintf(chaine, "PID %d : %d-%d\n", ancienActif->pid, ancienActif->tempsDebut, temps);
                    write(pipe[1], chaine, strlen(chaine));

                }

            } else if (!listeVide(pret)) {
                //On a un processus, donc on imprime le idle avant
                if (idle) {
                    sprintf(chaine, "%s : %d-%d\n", IDLE, debutIdle, temps);
                    write(pipe[1], chaine, strlen(chaine));
                    idle = 0;
                }
                actif = choisirProcessus(pret, algo);
                actif->tempsDebut = temps;

                if(ancienActif && (compareProcessus((void *)actif, (void *)ancienActif) != 0)) {
                    sprintf(chaine, "PID %d : %d-%d\n", ancienActif->pid, ancienActif->tempsDebut, temps);
                    write(pipe[1], chaine, strlen(chaine));
                } 
            }
        }
        temps++;

    }
    //On libère l'espace utilisé
    free(pret);
    free(bloque);
    position = l->debut;
    while(position != NULL) {
        temp = position->suivant;
        libererProcessus((Processus *)position->contenu);
        free(position);
        position = temp;
    }
    free(l);

    return;
}

//Retourne l'etat du processus donne apres traitement
enum Etat traiterActif(Processus *p, int temps, int quantum,  enum Algorithme algo) {

    Noeud *temp;

    //On met à jour le temp actif et le temps restant (soit pour un blocage, 
    //soit pour la fin.
    p->tempsActif++;
    --(*(int *)(p->tempsProc->debut->contenu));
    if((*(int *)(p->tempsProc->debut->contenu)) <= 0) {
        p->tempsFin = temps;
        temp = p->tempsProc->debut;
        p->tempsProc->debut = p->tempsProc->debut->suivant;
        free((int*)temp->contenu);
        free(temp);
        if(p->tempsProc->debut == NULL) {
            p->tempsProc->fin = NULL;
            return FIN;
        } else {
            p->tempsActif = 0;
            return BLOQUE;
        }
    }
    if(algo == RR) {
       if(p->tempsActif >= quantum) {
           p->tempsActif = 0;
           return PRET;
       }
    }
    return ACTIF;
}

void libererProcessus(Processus *p) {

    assert(p && "libere un processus NULL");
    Noeud *n = p->tempsProc->debut;
    Noeud *suivant;

    while (n != NULL) {
        suivant = n->suivant;
        free(n->contenu);
        free(n);
        n = suivant;
    }
    free(p->tempsProc);
    free(p);
}

Processus * choisirProcessus(Liste *pret, enum Algorithme algo) {

    assert(pret->taille > 0);
    Processus *p;
    Noeud *iterateur;
    

    if(algo == RR) {
        //Round robin: on prend le premier élément de la liste
        p = (Processus *)pret->debut->contenu;
    } else {
        //C'est SJF ou SJFP: meme chose à faire
        p = (Processus *)pret->debut->contenu;
        iterateur = pret->debut->suivant;
        while(iterateur != NULL) {
            if(compareSJF((const void*)p, iterateur->contenu) > 0) {
                p = (Processus *)iterateur->contenu;
            }
            iterateur = iterateur->suivant;
        }
    }
    listeRetirer(pret, p, compareProcessus);
    return p;
}


//Ici on compare le temps jusqu'au prochain blocage. S'ils sont identiques, on prend
//on retourne une valeur pour avoir la plus petite entrée dans l'ordre de lecture
//du fichier
int compareSJF(const void* p1, const void* p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    int temp;
    temp = (*((int *)pr1->tempsProc->debut->contenu)) - (*((int *)pr2->tempsProc->debut->contenu));
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
    Noeud *temp = (Noeud*)malloc(sizeof(Noeud));
    temp->contenu = p;
    temp->suivant = NULL;
    if(l->debut == NULL) {
        //Liste vide
        l->debut = temp;
        l->fin = temp;
        l->taille++;
    } else {
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

    del = listeChercher(l, p, (*compare), &prec);
    if(del != NULL) {

        //La recheche est ok
        l->taille--;
        //Cas du dernier élément de la liste
        if(l->taille == 0) {
            l->debut = NULL;
            l->fin = NULL;
        } else if((*compare)(l->debut->contenu, del->contenu) == 0) {
            l->debut = del->suivant;
            return l->debut;
        } else if((*compare)(l->fin->contenu, del->contenu) == 0) {
            prec->suivant = NULL;
            l->fin = prec;
        } else {
            temp = del;
            prec->suivant = del->suivant;
        }
    }
    if(del)
        free(del);
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


//Retourne 0 si aucun nouveau processus pret, 1 sinon;
int traiterListeBloque (Liste *bloque, Liste *pret, int (*compare)(const void *p1, const void *p2)) {

    int verifProc = 0;
    //Maintenant on traite les processus bloques
    Noeud *posBloque = bloque->debut, *aLiberer;
    Processus *temp;    
    while (posBloque != NULL) {
        //On met à jour le temp de blocage
        temp = (Processus *)posBloque->contenu;
        (*((int *)(temp->tempsProc->debut->contenu)))++;
        if((*((int *)(temp->tempsProc->debut->contenu))) >= 0) {
            //Plus bloque: termine ou pret?
            aLiberer = temp->tempsProc->debut;
            temp->tempsProc->debut = temp->tempsProc->debut->suivant;
            free((int *)aLiberer->contenu);
            free(aLiberer);
            if(temp->tempsProc->debut == NULL) {
                temp->tempsProc->fin = NULL;
                posBloque = listeRetirer(bloque, (void*)temp, (*compare));
            } else {
                verifProc = 1;
                //Processus passe à prêt
                listeInserer(pret, (void*)temp);
                posBloque = listeRetirer(bloque, (void*)temp, compareProcessus);
            }
        } else {
            posBloque = posBloque->suivant;
        }
    }
    return verifProc;
}

