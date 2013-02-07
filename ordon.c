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
#define ERR_ARG 1
#define ERR_QUANTUM 2
#define ERR_FICHIER 3
#define TAILLE_T  10 //Taille minimale du tableau de processus
#define MAX_LIG 255


typedef struct {
    int pid;
    int arrive;
    int* tempsProc;
    int tailleTempsProc;        //Pour savoir combien d'éléments dans tempsProc
} Processus;

enum {PROCESSUS, ENTIER};

//Lire_fichier: traite le fichier donné en argument et retourne un tableau de Processus
Processus* lireFichier(FILE* fichier);


int main(int argc, char *argv[]) {

    int quantum;
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
    if((fichier = fopen(argv[1], "r")) != NULL) {
        fprintf(stderr, "Erreur lors de l'ouverture du fichier.\n");
        exit(ERR_FICHIER);
    }
    p = lireFichier(fichier);
    close(fichier);

    return 0;
}

Processus* lireFichier(FILE* fichier) {

    int tailleProcMax = TAILLE_T, tailleEntMax = TAILLE_T;
    int taille = 0;
    int etat, temp, position, neg; //neg: 0 faux, 1 vrai
    Processus* p = (Processus)malloc(tailleProcMax*sizeof(Processus));
    char ligne[MAX_LIG];

    while((ligne = fgets(fichier)) != NULL) {
        //On insère dans le tableau
        //Debut: le pid
        if(taille == tailleProcMax) {
            taillePrcMax = redimensionner(p, tailleProcMax, PROCESSUS);
        }
        etat = sscanf(ligne, "%d", p[taille].pid);
        etat = sscanf(ligne, "%d", p[taille].arrive);
        neg = 0;
        position = 0;
        p[taille].tempsProc = (int)malloc(tailleEntMax*sizeof(int));
        while((etat = sscanf(ligne, "%d", temp)) != EOF) {
            if(position >= tailleEntMax) {
                tailleEntMax = redimensionner(p[taille].tempsProc, tailleEntMax, ENTIER);
            }
            if((temp < 0 && neg == 0) || (temp >0 && neg == 1)) {
                p[taille].tempsProc[++position] = temp;
                neg = (neg==0?1:0);
            } else if ((temp < 0 && neg == 1) || (temp > 0 && neg == 0)) {
                p[taille].tempsProc[position] += temp;
            }
        }
        p[taille++].tailleTempsProc = position+1;
    }

    return p;
}
