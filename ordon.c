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

enum Etat {ATTENTE, PRET, ACTIF, BLOQUE, FIN};

enum Algo {SJF, SJFP, RR};

typedef struct {
    int tempsActif;
    int pid;
    int arrive;
    int tempsTotalProc;          //Pour SJF
    int* tempsProc;
    int positionTempsProc;
    int tailleTempsProc;        //Pour savoir combien d'éléments dans tempsProc
    enum Etat etat;
} Processus;

//Lire_fichier: traite le fichier donné en argument et retourne un tableau de Processus
int lireFichier(FILE* fichier, Processus** p);

int compare(const void *p1, const void* p2);

void traiterSJF(Processus *p, int taille, int *writePipe);
void traiterSJFP(Processus *p, int taille, int *writePipe, int quantum);

void traiterAttente(Processus *p, int temps);
int traiterBloque(Processus *p);
int traiterActif(Processus **p, int *pipe, int temps, int quantum, enum Algo algo);
Processus * choisirProcessus(Processus **pret, int taillePret);
int compareSJF (const void *p1, const void *p2);


int main(int argc, char *argv[]) {

    int quantum, readPipe1[2] = {-1, -1}, readPipe2[2] = {-1, -1}, vtfr, tailleP, status, i, j;
    char buf;
    FILE* fichier;
    Processus* p;
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
    tailleP = lireFichier(fichier, &p);
    fclose(fichier);

    if(pipe(readPipe1) < 0) {
        fprintf(stderr, "Erreur de création du pipe.\n");
        exit(ERR_PIPE);
    }

    //On fait un quicksort
    qsort(p, tailleP, sizeof(Processus), compare);

    for(i = 0; i < tailleP; i++) {
        printf("PID %d arrive %d : ", p[i].pid, p[i].arrive);
        for(j = 0; j < p[i].tailleTempsProc; j++) {
            printf("%d ", p[i].tempsProc[j]);
        }
        printf("\n");
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
        traiterSJF(p, tailleP, readPipe1);
    }

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
    }
    return 0;
}

int lireFichier(FILE* fichier, Processus** p) {

    int i;
    int tailleProcMax = TAILLE_T, tailleEntMax = TAILLE_T;
    int taille = 0;
    int temp, position, neg; //neg: 0 faux, 1 vrai
    Processus* pr = (Processus *)malloc(tailleProcMax*sizeof(Processus));
    char ligne[MAX_LIG], *ptrLig, *e;

    while((fgets(ligne, MAX_LIG, fichier)) != NULL) {
        //On insère dans le tableau
        //Debut: le pid
        if(taille == tailleProcMax) {
            tailleProcMax *= 2;
            pr = (Processus *)realloc(pr, tailleProcMax*sizeof(Processus));
        }
        neg = 0;
        position = 0;
        pr[taille].tempsProc = (int *)malloc(tailleEntMax*sizeof(int));
        pr[taille].positionTempsProc = 0;
        pr[taille].etat = ATTENTE;
        pr[taille].tempsActif = 0;
        for(i = 0,ptrLig = ligne; ;ptrLig = e, i++) {
            temp = (int)strtol(ptrLig, &e, 10);
            if(ptrLig == e) {
                break;
            }
            if(i == 0 || i == 1) {//C'est le PID
                if(temp <= 0) {
                    fprintf(stderr, "Valeur de PID ou temp d'arrive illégale.\n");
                    exit(ERR_PID);
                }
                if(i == 0) {
                    pr[taille].pid = temp;
                }
                else {
                    pr[taille].arrive = temp;
                }
            } else { //C'est une info

                if(i >= tailleEntMax) {
                    tailleEntMax *= 2;
                    pr[taille].tempsProc = (int *)realloc(pr[taille].tempsProc, tailleEntMax*sizeof(int));
                }
                if((temp < 0 && neg == 0) || (temp >0 && neg == 1)) {
                    pr[taille].tempsProc[++position] = temp;
                    neg = (neg==0?1:0);
                    pr[taille].tempsTotalProc += (temp>0?temp:0);
                } else if ((temp < 0 && neg == 1) || (temp > 0 && neg == 0)) {
                    pr[taille].tempsProc[position] += temp;
                    pr[taille].tempsTotalProc += (temp>0?temp:0);
                }
            }
        }
        assert(i >= 3 && "Manque d'information pour un processus.");
        pr[taille++].tailleTempsProc = position+1;
    }
    *p = pr;
    return taille;
}

int compare(const void *p1, const void *p2) {
    Processus *temp1 = (Processus *)p1;
    Processus *temp2 = (Processus *)p2;
    return temp1->arrive - temp2->arrive;
}

void traiterSJF(Processus *p, int taille, int *writePipe) {

    Processus **pret = (Processus **)malloc(taille*sizeof(Processus *));
    int taillePret, i, fin, quantum = 1;
    Processus *actif = NULL;
    int temps = 0, idle = 0, debutIdle;
    char chaine[20];

    while(1) {
        taillePret = 0;
        fin = 1;
        actif = NULL;
        for(i = 0; i < taille; i++) {
            if(p[i].etat == ATTENTE) {
                traiterAttente(&p[i], temps);
                fin = 0;
            }
            if(p[i].etat == BLOQUE) {
                traiterBloque(&p[i]);
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
            traiterActif(&actif, writePipe, temps, quantum, SJF);
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


//Retourne 1 si pret, 0 si bloque ou fin, -1 si aucun des cas
int traiterActif(Processus **p, int *pipe, int temps, int quantum, enum Algo algo) {

    char chaine[10];
    (*p)->tempsTotalProc--;
    (*p)->tempsActif++;
    if((--((*p)->tempsProc[(*p)->positionTempsProc])) <= 0) {
        sprintf(chaine, "%d\n", temps);
        write(pipe[1], chaine, strlen(chaine));
        if((++((*p)->positionTempsProc)) >= (*p)->tailleTempsProc) {
            (*p)->etat = FIN;
        } else {
            (*p)->etat = BLOQUE;
        }
        return 0;
    } else if (algo == SJFP && (*p)->tempsActif >= quantum) {
        sprintf(chaine, "%d\n", temps);
        write(pipe[1], chaine, strlen(chaine));
        (*p)->tempsActif = 0;
        (*p)->etat = PRET;
        return 1;
    } else {
        return -1;
    }
}

Processus * choisirProcessus(Processus **pret, int taillePret) {
    //Premierement, on classe les processus en ordre de temps de processus restant
    qsort(pret, taillePret, sizeof(Processus), compareSJF);
    (*pret[0]).etat = ACTIF;
    return pret[0];
}

int compareSJF(const void* p1, const void* p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    return pr1->tempsTotalProc - pr2->tempsTotalProc;
}
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

