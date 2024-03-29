/* ordon.c
 * Par Guillaume Lahaie
 * LAHG04077707
 * Dernière modification: 23 février 2013
 *
 * Ce programme lit en entrée un fichier contenant des temps d'utilisations 
 * de processus pour un ordonnanceur. Le programme crée 3 processus fils qui
 * ensuite font l'ordonnancement des processus selon différents algorithmes
 * Le premier le fait selon SJF, ensuite le second selon SJFP, et finalement
 * round robin. Ils mettent les informations dans un pipe pour renvoyer au
 * pere, qui affiche les résultats, une fois que tous les processus fils
 * se sont terminés.
 *
 * Pour simuler les différents algorithmes d'ordonnance, et pour traiter un 
 * nombre inconnu de processus et de temps d'utilisation du processeur ou de
 * blocage, j'utilise des listes chainées. J'ai tout d'abord utilisé un
 * tableau de processus, mais je trouvais que cela s'éloignait trop du
 * fonctionnement apris en classe. Toutefois, l'utilisation de listes
 * alourdi le code présenté ici car, pour utiliser seulement un fichier,
 * je défini la structure de la liste et les fonctions de gestion dans
 * le fichier.
 *
 * Le programme vérifie tout d'abord si les arguments sont légaux. Le 
 * programme s'arrete s'il n'a pas exactement 3 arguments, si le 
 * troisième argument, le quantum, contient des caractères différents
 * de chiffres et si sa valeur est plus petite que 1. Pour le nom du
 * fichier, on vérifie s'il le fichier existe, s'il contient des informations
 * (au moins une ligne valide décrivant un processus, la ligne doit
 * contenir au moins 3 nombres: le pid et le temps d'arrive, qui doivent
 * être plus grand que 0, et au moins un temps d'exécution, je ne vérifie
 * pas si le temps est positif. Un processus pourrait donc avoir seulement
 * un temps de blocage), et aussi si le fichier contient des caractères
 * différents d'espace (donc isspace() est vrai) ou des chiffres.
 *
 * Pour chaque ligne du fichier, le programme crée un processus, qui est
 * inséré dans la liste de processus. Une fois le fichier traité, le 
 * processus père crée 3 processus fils qui traite la liste selon
 * l'ordonnancement demandé. Les processus fils font le traitement et
 * écrivent les résultats dans des pipes pères, et ensuite se terminent.
 * Le père lit l'information des trois pipes et insères les résultats
 * dans des string.
 *
 * Une fois les trois processus fils terminés, le processus père affiche
 * les résultats des trois ordonnancements demandés.
 *
 * Pour la simulation des ordonnancements, j'utilise un pointeur de
 * processus pour le processus actif, une liste pour les processus
 * bloqués et une liste pour les processus prêts. Pour respecter la règle
 * du TP que si le processus d'ordonnancement a à choisir entre deux
 * processus, il prend le plus jeune, je garde le numéro de ligne dans la
 * structure Processus, et j'utilise une liste temporaire qui sera classée
 * afin de respecter cette règle. C'est surtout un problème pour 
 * l'ordonnancement round-robin, où j'utilise la liste comme une file.
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
#include <ctype.h>
#define TAILLE_T  50            //Taille initiale d'un string.
#define NOMBRE_ALGO 3
#define SJF_S "SJF"
#define SJFP_S "SJFP"
#define RR_S "RR"
#define DELIM " "               //Pour strtok() 


//Enumerations::
//
//Erreur: pour les différentes possibilités d'arret du programme lors d'une
//erreur
enum Erreur {ERR_ARG = 1, ERR_QUANTUM, ERR_FICHIER, ERR_PID, ERR_PIPE, 
             ERR_MALLOC, ERR_FORK};

//Différents algorithmes que le programme traite
enum Algorithme {SJF, SJFP, RR};

//Etat posssibles d'un processus
enum Etat {PRET, ACTIF, BLOQUE, FIN};

//Pour ajouterCar: soit pour la lecture du fichier (LIGNE), ou pour la lecture
//du pipe(TEXTE)
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

//Structure pour un processus
typedef struct {
    int tempsDebut; //Temps ou le processus est elu actif
    int ligneFich;  //Pour départager deux processus arrivant en même temps
    int tempsActif; //Pour vérifier le quantum
    int pid;        
    int arrive;     
    Liste *tempsProc;   //Liste des temps de traitement et de blocage.
} Processus;

//-----------------------------------------------------------------------------
//Fonctions de traitement de lecture de fichier ou d'écriture
//
//lireFichier: traite le fichier donné en argument et retourne un tableau
//de Processus
Liste *lireFichier(FILE* fichier);

//lirePipe: lit le pipe donné tant qu'il y a des caractères et les insère
//dans une chaine de caractère qui grandit de façon dynamique. À la fin
//retourne cette chaine de caractère
char * lirePipe(int *pipefd, int vtfr);

//ecrirePipe: ecrit le resultat du prochain processus sur le pipe
void ecrirePipe(int *pipefd, Processus *p, int temps, int tempsIdle);

//ajouterCar: ajoute le caractère prochCar à la chaine chaine. Si la chaine
//est pleine, on l'agrandit alors avec realloc.
char * ajouterCar(int prochCar, char *chaine, int *position, 
        int *tailleTotale, enum Ecriture type);

//-----------------------------------------------------------------------------
//Fonctions de traitement des processus
//
//traiterProcessus: fonction de base: elle traite les processus présents dans
//la liste tant qu'il reste des nouveaux, qu'il y a un processus actif, au moins
//un processus bloqué ou un processus prêt. Elle traite la liste selon 
//l'algorithme demandé
void traiterProcessus(Liste *l, int *pipefd, int quantum, enum Algorithme algo);

//traiterListeBloque: ajuste le temps qu'il reste au processus bloque. Si le
//processus n'est plus bloqué, il est enlevé de la liste bloque et est insérer
//dans la liste prêté On retourne une valeur indiquant si un processus est
//passé de bloqué à prêt
int traiterListeBloque (Liste *bloque, Liste *pret);

//traiterActif: ajuste le temps d'exécution du processus actif. Selon 
//l'algorithme donnée, vérifie si le quantum est terminé, si le processus
//est maintenant bloqué, ou s'il est terminé.
enum Etat traiterActif(Processus *p, int quantum, enum Algorithme algo, 
        Liste *bloque, Liste *pret, int preemption);

//choisirProcessus: choisi le prochain processus à exécuter, selon l'algorithme
//donné, de la liste des prêts. Le processus est alors retiré de la liste
//des prêts et est retourné par la fonction.
Processus * choisirProcessus(Liste *pret, enum Algorithme algo);

//libererProcessus: Lorsqu'un processus est terminé, on libère la mémoire 
//dynamiquement allouée pour ce processus.
void libererProcessus(Processus *p);


//libererListeProcessus: On libère la mémoire allouée pour la liste de processus
void libererListeProcessus(Liste *l);

//-----------------------------------------------------------------------------
//Comparateurs différents pour listes: retourne un nombre négatif si p1 est
//plus petit que p2, 0 si identique, > 0 si p1 est plus grand que p2, selon
//les critères de comparaisons.

//compareArrive: compare deux processus pour trouver celui ayant le plus
//petit temps d'arrivé. S'ils sont identiques, on compare le numér de ligne
//du fichier.
int compareArrive(const void *p1, const void *p2);

//compareSJF: vérifie lequel des 2 processus est le plus près de son prochain
//blocage, ou de sa terminaison. Si les temps sont égales, on compare le numéro
//de ligne de fichier
int compareSJF (const void *p1, const void *p2);

//compareProcessus: pour trouver un processus à retirer d'une liste. Je considère
//qu'un processus est unique selon son PID et son temps d'arrivé.
int compareProcessus(const void *p1, const void *p2);

//compareLigFichier: compare les numéros de ligne de fichier de deux processus.
int compareLigFichier(const void *p1, const void *p2);


//--------------------------------------------------------------------------------
// Fonctions pour la gestion des listes chainées.

//listeCreer: crée une liste de façon dynamique et retourne un pointeur
Liste *listeCreer();

//listeInserer: insère un élément dans un nouveau noeud. L'élément est insérer
//à la fin de la liste.
Noeud *listeInserer(Liste *l, void *p);

//listeRetirer: retire l'élément donné de la liste, si l'élément est présent
//dans la liste.
void listeRetirer(Liste *l, void *p);

//listeChercher: cherche la liste l pour vérifier si l'élément p est présent,
//avec la fonction de comparaison donnée. Si l'élément est présent, on retourne
//un lien vers le noeud, sinon on retourne NULL. Le noeud prec est utilise
//pour pouvoir facilement retirer un element de la liste.
Noeud *listeChercher(Liste *l, void *p, Noeud **prec);

//listeVide: vérifie si la liste donnée est vide. Retourne vrai (1) si
//elle est vide, faux (0) sinon.
int listeVide(Liste *l);

//listeQuickSort: appelle la fonction quickSort sur les noeuds, et ajuste
//ensuite les pointeurs debut et fin.
void listeQuickSort(Liste *l, int (*compare)(const void *p1, const void *p2));

//quickSort: trie les noeuds selon la fonction de comparaison donnée.
Noeud *quickSort(Noeud *n, int (*compare)(const void *p1,const void *p2));

//chercherPivot: trouve un pivot pour effectuer quickSort
Noeud *chercherPivot(Noeud *n);

//listeLongueurPartielle: trouve la longueur de la liste à partir du 
//noeud donnée.
int listeLongueurPartielle(Noeud *n);

//-----------------------------------------------------------------------------


int main(int argc, char *argv[]) {

    int quantum,            
        pipefd[NOMBRE_ALGO][2],      //Pipes pour les fils - lire les résultats
        vtfr[NOMBRE_ALGO],           //Valeurs de fork pour le père
        i, j;
    char *resultat[NOMBRE_ALGO],     //Résultats des processus fils
         *e;
    FILE* fichier;
    Liste *l;


    //Début: on vérifie les arguments
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <nom_fichier> <quantum>\n", argv[0]);
        exit(ERR_ARG);
    }

    //On vérifie aussi si le quantum est légale (donc > 0) et contenant
    //seulement des nombres
    quantum = strtol(argv[2], &e, 10);
    if(quantum <= 0 || *e != '\0') {
        fprintf(stderr, "%s: Erreur: quantum illegal.\n", argv[0]);
        exit(ERR_QUANTUM);
    }

    //Ouverture  et lecture du fichier
    if((fichier = fopen(argv[1], "r")) == NULL) {
        fprintf(stderr, "Erreur lors de l'ouverture du fichier.\n");
        exit(ERR_FICHIER);
    }
    
    l = lireFichier(fichier);
    fclose(fichier);
    if(!l) {
        fprintf(stderr,"Erreur de lecture de fichier: aucune information lue.\n");
        exit(ERR_FICHIER);
    }

    //On trie la liste en ordre d'arrive des processus
    listeQuickSort(l, compareArrive);

    //On traite les algorithmes
    for(i = 0; i < NOMBRE_ALGO; i++) {
        resultat[i] = NULL;
        if(pipe(pipefd[i]) < 0) {
            perror("pipe");
            exit(ERR_PIPE);
        }
        if((vtfr[i] = fork()) < 0) {
            perror("fork");
            exit(ERR_FORK);
        }
        if(vtfr[i] != 0) {
            resultat[i] = lirePipe(pipefd[i], vtfr[i]);
        } else {
            //Libere la mémoire des résultats déjà calculés par le père
            for(j = 0; j < i; j++) {
                if(resultat[j])
                    free(resultat[j]);
            }
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
    for(i = 0; i < NOMBRE_ALGO; i++) {
        printf("%s\n", resultat[i]);
        free(resultat[i]);
    }

    //Liberation de l'espace utilisé
    libererListeProcessus(l);
    
    return 0;
}

//-----------------------------------------------------------------------------
//Fonctions de lecture de fichier et de pipe

Liste *lireFichier(FILE* fichier) {

    int i, *intListe, noLig = 0, prochCar;
    int prochInt, 
        neg;        //Est-ce que le nombre est négatif?
    int longueurMaxLigne = TAILLE_T, longueurLigne = 0;
    Processus *p;
    char *ligne = (char *)malloc(TAILLE_T*sizeof(char)), *ptrLig, *e;
    Liste *l = listeCreer();

    while((prochCar = fgetc(fichier)) != EOF) {
  
        //On lit tous les chars jusqu'à un différent d'un espace
        while(isspace(prochCar)) {
            prochCar = fgetc(fichier);
        }
        //Ici, vérifier qu'on n'est pas à la fin du fichier
       if(prochCar != EOF) {
        
            //On garde la même ligne qu'auparavant, mais on ecrase son
            //contenu.
            longueurLigne = 0;

            //On rempli la ligne
            while(1) {
                ligne = ajouterCar(prochCar, ligne, &longueurLigne, 
                        &longueurMaxLigne, LIGNE);
                if(prochCar == '\n' || prochCar == EOF)
                    break;
                prochCar = fgetc(fichier);
            }

            //On a une ligne de forme string bien formée, on la traite
            intListe = NULL;
            neg = 1;
            if(!(p = (Processus *)malloc(sizeof(Processus)))) {
                fprintf(stderr, "Erreur d'allocation de memoire\n");
                exit(ERR_MALLOC);
            }
            p->tempsProc = listeCreer();
            p->tempsActif = 0;

            for(i = 0, ptrLig = strtok(ligne, DELIM); ptrLig != NULL;
                    ptrLig = strtok(NULL, DELIM), i++) 
            {
                prochInt = (int)strtol(ptrLig, &e, 10);
                if(!isspace(*e) && *e != '\0') {
                    fprintf(stderr, "Erreur: formattage de fichier illegal.\n");
                    exit(ERR_FICHIER);
                }
            
                if((i == 0 || i == 1) && prochInt < 0 ) {  
                    fprintf(stderr, "Valeur de PID ou temp d'arrive illégale.\n");
                    exit(ERR_PID);
                }
                
                if(i == 0) {
                    p->pid = prochInt;
                } else if (i == 1) {
                    p->arrive = prochInt;
                } else { 

                    if((prochInt < 0 && neg == 0) || intListe == NULL ||
                            (prochInt >= 0 && neg == 1)) 
                    {
                        //Premier chiffre ou on change de signe: nouvelle
                        //entrée dans la liste
                        if(!(intListe = (int*)malloc(sizeof(int)))) {
                            fprintf(stderr, "Erreur d'allocation de memoire\n");
                            exit(ERR_MALLOC);
                        }
                        *intListe = prochInt;
                        listeInserer(p->tempsProc, intListe);
                        neg = (neg==0?1:0);
                    } else if ((prochInt < 0 && neg == 1) || 
                            (prochInt >= 0 && neg == 0)) 
                    {
                        *((int *)p->tempsProc->fin->contenu) += prochInt;
                    }
                }
            }
            assert(i >= 3 && "Manque d'information pour un processus.");
            p->ligneFich = ++noLig;
            listeInserer(l, p);
        }
    }

    if(ligne)
        free(ligne);
    if(!l || l->taille < 1) {
        free(l);
        return NULL;
    }
    return l;
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

void ecrirePipe(int *pipefd, Processus *p, int temps, int tempsIdle) {
    char chaine[50];
    if(p == NULL) {
        sprintf(chaine, "IDLE : %d-%d\n", tempsIdle, temps);
    } else {
        sprintf(chaine, "PID %d : %d-%d\n", p->pid, 
                p->tempsDebut, temps);
    }
    write(pipefd[1], chaine, strlen(chaine));
}



char * ajouterCar(int prochCar, char *chaine, int *position, int *tailleMax, 
        enum Ecriture type) 
{
    char *temp = NULL;

    //Vérifie si on doit allouer plus d'espace
    if((*position) >= (*tailleMax)) {
        (*tailleMax) = (*tailleMax) * 2;
        
        temp = (char *)realloc(chaine, *tailleMax);
        if(temp) {
            chaine = temp;
        }
        else {
            fprintf(stderr, "erreur de realloc\n");
            exit(ERR_MALLOC);
        }
    }

    //Pour la réutilisation de la fonction: traite t-on le fichier ou
    //la lecture d'un pipe?
    if(type == LIGNE) {
        chaine[(*position)++] = (char)((prochCar=='\n' || prochCar == EOF)?
                '\0':prochCar);
    } else {
        chaine[(*position)++] = (char)prochCar;
    }
    return chaine;
}

//-----------------------------------------------------------------------------
//Fonctions de simulation des algorithmes

void traiterProcessus(Liste *l, int *pipe, int quantum, enum Algorithme algo) {

    Liste *pret = listeCreer();
    Liste *bloque = listeCreer();
    Liste *nouveauPret = listeCreer();      //Pour s'assurer dans RR que les
                                            //processus sont dans le bon ordre,
                                            //selon les exigences du TP.
    Noeud *position = l->debut, *temp;
    Processus *actif = NULL, *ancienActif;  //ancienActif: vérifie s'il y a
                                            //vraiment un changement de contexte
    int temps = 0, 
        idle = 0, debutIdle,                //vérifie si le cpu est idle, et
                                            //depuis quand
        preemption;                         //Pour SJFP
    char chaine[50];                        //chaine pour écrire sur le pipe
    enum Etat changement;                   

    char enTete[] = "Resultat du processus pour: ";
    write(pipe[1], enTete, strlen(enTete));
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
        while(position != NULL && 
                ((Processus *)(position->contenu))->arrive <= temps) 
        {
            //Vérifie ici si le processus est bloqué au début: logiquement,
            //ne devrait pas se produire
            if(*((int *)(((Processus *)position->contenu)->
                            tempsProc->debut->contenu)) < 0) {
                listeInserer(bloque, position->contenu);
            } else {
                if(algo == SJFP)
                    preemption = 1;
                listeInserer(nouveauPret, position->contenu);
            }
            position = position->suivant;
        }
        
        //Maintenant on traite les processus bloques, au besoin
        if(bloque->taille > 0)
            preemption += traiterListeBloque(bloque, nouveauPret);

        //Maintenant on traite le processus actif, s'il y en a un
        if(actif) {
            changement = traiterActif(actif, quantum, algo, bloque, 
                    nouveauPret, preemption);
            ancienActif = (changement != ACTIF || (algo == SJFP && preemption))?
                            actif:NULL;
            actif = (changement != ACTIF || (algo == SJFP && preemption))?
                        NULL:actif;
        }

        //On a tous les nouveaux processus à ajouter à pret. On les ajoute.
        if(algo == RR && nouveauPret->taille > 1) {
            listeQuickSort(nouveauPret, compareLigFichier);
        }
        temp = nouveauPret->debut;
        while(temp) {
            listeInserer(pret, temp->contenu);
            listeRetirer(nouveauPret, temp->contenu);
            temp = nouveauPret->debut;
        }

        //Si actif est NULL, on doit élir un nouveau processus, ou
        //mettre le processeur à IDLE. 
        if(!actif) {
            if(listeVide(pret) && !idle) {
                //Aucun processus dans pret, on change le processus à IDLE
                debutIdle = temps;
                idle = 1;
                if(ancienActif) {
                    ecrirePipe(pipe, ancienActif, temps, debutIdle);
                }

            } else if (!listeVide(pret)) {
                //Deux cas à traiter: si le processeur est IDLE, on doit
                //écrire, s'il ne l'était pas, on regarde pour vérifier
                //si on change vraiment de processus.
                if (idle) {
                    ecrirePipe(pipe, NULL, temps, debutIdle);
                    idle = 0;
                }
                actif = choisirProcessus(pret, algo);

                if(ancienActif && (compareProcessus((void *)actif, 
                                (void *)ancienActif) != 0)) 
                {
                    actif->tempsDebut = temps;
                    ecrirePipe(pipe, ancienActif, temps, debutIdle);
                } else if (!ancienActif)
                    actif->tempsDebut = temps;
            }
        }

        temps++;

    }
    //On libère l'espace utilisé et ferme le pipe.
    close(pipe[0]);
    free(pret);
    free(bloque);
    free(nouveauPret);
    libererListeProcessus(l);
    return;
}

enum Etat traiterActif(Processus *p, int quantum,  enum Algorithme algo, 
        Liste *bloque, Liste *pret, int preemption) 
{

    assert(p && "p est NULL");
    Noeud *temp;

    //On met à jour le temp actif et le temps restant (soit pour un blocage, 
    //soit pour la fin.
    p->tempsActif++;
    if((--(*(int *)(p->tempsProc->debut->contenu))) <= 0) {
        temp = p->tempsProc->debut;
        p->tempsProc->debut = p->tempsProc->debut->suivant;
        free((int*)temp->contenu);
        free(temp);
        if(p->tempsProc->debut == NULL) {
            //Fin du processus: rien à faire
            p->tempsProc->fin = NULL;
            return FIN;
        } else {
            //Processus bloque: on l'ajoute à bloque.
            p->tempsActif = 0;
            listeInserer(bloque, (void *)p);
            return BLOQUE;
        }
    }
    if((algo == RR && p->tempsActif >= quantum) || (algo == SJFP && preemption)) 
    {
        p->tempsActif = 0;
        listeInserer(pret, (void *)p);
        return PRET;
    }
    return ACTIF;
}

Processus * choisirProcessus(Liste *pret, enum Algorithme algo) {

    assert(pret && pret->taille > 0 && "pret est vide");
    Processus *p = (Processus *)pret->debut->contenu;
    Noeud *iterateur;
    
    if(algo != RR) {
        //C'est SJF ou SJFP: meme chose à faire
        iterateur = pret->debut->suivant;
        while(iterateur != NULL) {
            if(compareSJF((const void*)p, iterateur->contenu) > 0) {
                p = (Processus *)iterateur->contenu;
            }
            iterateur = iterateur->suivant;
        }
    }
    listeRetirer(pret, p);
    return p;
}

int traiterListeBloque (Liste *bloque, Liste *pret) {

    assert(bloque && bloque->taille > 0 && "bloque n'a pas d'élément");

    int preemption = 0;
    //Maintenant on traite les processus bloques
    Noeud *posBloque = bloque->debut, *aLiberer;
    Processus *temp;    
    while (posBloque != NULL) {
        //On met à jour le temp de blocage
        temp = (Processus *)posBloque->contenu;
        if((++(*((int *)(temp->tempsProc->debut->contenu)))) >= 0) {
            //Plus bloque: termine ou pret?
            aLiberer = temp->tempsProc->debut;
            temp->tempsProc->debut = temp->tempsProc->debut->suivant;
            
            //On libère le noeud de la liste des entiers.
            free((int *)aLiberer->contenu);
            free(aLiberer);
            if(temp->tempsProc->debut == NULL) {
                //Le processus est terminé après un blocage. Logiquement
                //ne devrait pas se passer, mais on le traite.
                temp->tempsProc->fin = NULL;
            } else {
                preemption = 1;
                //Processus passe à prêt
                listeInserer(pret, (void*)temp);
            }
            posBloque = posBloque->suivant;
            listeRetirer(bloque, (void*)temp);

        } else {
            posBloque = posBloque->suivant;
        }
    }
    return preemption;
}


void libererListeProcessus(Liste *l) {

    Noeud *position = l->debut, *temp;

    while(position != NULL) {
        temp = position->suivant;
        libererProcessus((Processus *)position->contenu);
        free(position);
        position = temp;
    }
    free(l);
    return;
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

//-----------------------------------------------------------------------------
//Fonctions de comparaisons de processus

int compareArrive(const void *p1, const void *p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    int temp = pr1->arrive - pr2->arrive;
    if(temp != 0) 
        return temp;
        
    return compareLigFichier(p1, p2);
}

int compareSJF(const void* p1, const void* p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    int temp;
    temp = (*((int *)pr1->tempsProc->debut->contenu)) - 
            (*((int *)pr2->tempsProc->debut->contenu));
    if(temp != 0)
        return temp;
    else 
        return compareLigFichier(p1, p2);
}

int compareLigFichier(const void* p1, const void* p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    return pr1->ligneFich - pr2->ligneFich;
}

int compareProcessus(const void *p1, const void *p2) {
    Processus *pr1 = (Processus *)p1;
    Processus *pr2 = (Processus *)p2;
    int temp = pr1->pid - pr2->pid;
    if (temp != 0)
        return temp;
    else
        return pr1->arrive - pr2->arrive;
}

//--------------------------------------------------------------------------------
//Fonctions de gestion de liste chainée

Liste *listeCreer(void) {

    Liste *l;
    if(!(l = (Liste *)malloc(sizeof(Liste)))) {
        fprintf(stderr, "Erreur d'allocation de memoire.\n");
        exit(ERR_MALLOC);
    }
    l->debut = NULL;
    l->fin = NULL;
    l->taille = 0;
    return l;
}

Noeud *listeInserer(Liste *l, void *p) {

    assert(l && "La liste est NULL");
    Noeud *temp;
   
    if (!(temp =  (Noeud*)malloc(sizeof(Noeud)))) {
        fprintf(stderr, "Erreur d'allocation de memoire.\n");
        exit(ERR_MALLOC);
    }
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

void listeRetirer(Liste *l, void *p) 
{
    assert(l->taille > 0 && "Ne peut retirer d'une liste vide.");
    Noeud *del = NULL;
    Noeud *prec = NULL;

    del = listeChercher(l, p, &prec);
    if(del != NULL) {

        l->taille--;
        if(l->taille == 0) {
            //Cas du dernier élément de la liste
            l->debut = NULL;
            l->fin = NULL;
        } else if(compareProcessus(l->debut->contenu, del->contenu) == 0) {
            //Premier élément
            l->debut = del->suivant;
        } else if(compareProcessus(l->fin->contenu, del->contenu) == 0) {
            //Dernier élément
            prec->suivant = NULL;
            l->fin = prec;
        } else {
            //autre élément
            prec->suivant = del->suivant;
        }
        if(del) {
            free(del);
        }
    } else {
        fprintf(stderr, "Erreur de recherche: rien à effacer\n");
    }
    return;
}

Noeud *listeChercher(Liste *l, void *p, Noeud **prec) {
    
    assert(l->taille > 0 && "Ne peut chercher dans une liste vide.");
    Noeud *ptr = l->debut;
    Noeud *tmp = NULL;
    int trouve = 0;

    while(ptr != NULL) {
        if(!compareProcessus(ptr->contenu, p)) {
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
    assert(l && "Liste est NULL");
    return (l->taille == 0);
}


//--------------------------------------------------------------------------------
//Fonctions pour faire un quicksort sur la liste chainée.

void listeQuickSort(Liste *l, int (*compare)(const void *p1, const void *p2)) {

    assert(l != NULL && l->taille > 0);
    Noeud *temp1, *temp2;
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


