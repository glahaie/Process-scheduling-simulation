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

typedef struct {
    int pid;
    int arrive;
    int* tempsProc;
    int tailleTempsProc;        //Pour savoir combien d'éléments dans tempsProc
} Processus;

enum Type {PROCESSUS, ENTIER};

//Lire_fichier: traite le fichier donné en argument et retourne un tableau de Processus
int lireFichier(FILE* fichier, Processus** p);

int compare(const void *p1, const void* p2);

void SJF(Processus *p);

int main(int argc, char *argv[]) {

    int quantum, readPipe[2] = {-1, -1}, vtfr, tailleP, status, i, j;
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

    if(pipe(readPipe) < 0) {
        fprintf(stderr, "Erreur de création du pipe.\n");
        exit(ERR_PIPE);
    }

    //On fait un quicksort
    qsort(p, tailleP, sizeof(Processus), compare);

    vtfr = fork();
    if(vtfr != 0) {
        close(readPipe[1]);
        while(read(readPipe[0], &buf, 1) > 0) {
            printf("%c", buf);
        }
        printf("\n");
        close(readPipe[0]);
        wait(&status);
        exit(EXIT_SUCCESS);
    } else {
        close(readPipe[0]);
        SJF(p, taille, readPipe)
    }

    return 0;
}

int lireFichier(FILE* fichier, Processus** p) {

    int i, j;
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
        for(i = 0, ptrLig = ligne; ;ptrLig = e, i++) {
            temp = (int)strtol(ptrLig, &e, 10);
            if(ptrLig == e)
                break;
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
                } else if ((temp < 0 && neg == 1) || (temp > 0 && neg == 0)) {
                    pr[taille].tempsProc[position] += temp;
                }
            }
        }
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

void SJF(Processus *p, int taille) {

    Processus *pret = (Processus*)malloc(taille*sizeof(Processus));
    Processus *bloque = (Processus*)malloc(taille*sizeof(Processus));
    Processus *actif = NULL;
    Processus *nxt = p;
    int i, position;

    while(1) {
        
        if(actif) {
            //On a un processus actif, on avance tant qu'il ne bloque pas ou ne se termine pas
            while(actif->tempsProc[position] > 0) {
                i++;
                //Maintenant, il est soit bloqué, soit terminé


