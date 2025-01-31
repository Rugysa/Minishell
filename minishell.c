#include <bits/types/sigset_t.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>     // fork, getpid, getppid
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/** Varaibles globales*/

pid_t cmd_avant_plan; //Contient le pid de la commande en avant plan et 0 sinon
int choix_traitement_signal = 3; //Entier indiquant la manière dont sont traités les signaux 
                                // 1 : Utilisation de sigaction et association à traitement_SIGINT/SIGTSTP
                                // 2 : Utilisation de signal pour ignorer puis remettre le traitement par defaut de Ctrl c et Ctrl z
                                // 3 : Utilisation de liste contenant les signaux à masquer


/** Fonctions de tratement des différents signaux */

/* Fonction traitant le signal SIGCHLD*/
void traitement_SIGCHLD(int num_sig){
    int status;
    pid_t pid;
   
	while((pid = waitpid(-1, &status,WNOHANG)) >0){

        // Disjonction de cas en fonction du status du fils
        if(WIFSTOPPED(status)){
            printf("Je suis le processus %d et je suis suspendu\n", pid);
        }
        else if (WIFCONTINUED(status)){
            printf("Je suis le processus %d et j'ai été repris\n", pid);
        }
        else if(WIFEXITED(status)){
            printf("Je suis le processus %d et je me suis terminé\n", pid);
        } 
        else if (WIFSIGNALED(status)){
            printf("Je suis le processus %d et j'ai reçu le signal %d\n", pid,num_sig);
        }
          
        if(pid == cmd_avant_plan && (WIFSIGNALED(status) || WIFSTOPPED(status) || WIFEXITED(status))){
			//Si la commande en avant plan est suspendue, elle n'est plus en avanat plan
            cmd_avant_plan=0;
        }
    }
}

/* Fonction traitant le signal SIGINT*/
void traitement_SIGINT(int num_sig){
    printf("\n");
    printf("Vous avez réalisé la commande Ctrl C qui a pour signal %d\n",num_sig);
}

/* Fonction traitant le signal SIGTSTP*/
void traitement_SIGTSTP(int num_sig){
    printf("\n");
    printf("Vous avez réalisé la commande Ctrl Z qui a pour signal %d \n",num_sig);
}

/* Asoocie un signal à sa fonction de traitement */
void assocation_signal_handler(void traitement(int arg),int sig){
    //Initialisation du sigaction pour le signal sig
    struct sigaction action;
    action.sa_handler = traitement;
    sigemptyset(&action.sa_mask);
    action.sa_flags =SA_RESTART;
	//Envoie d'un signal concerant la terminaison d'un fils
    sigaction(sig, &action, NULL);
}

/* Fonction défissant la manière donc les signaux SIGCHLD, SIGINT et SIGTSTP sont traités */
void traitement_signaux(){

    //Traietement du signal SIGCHILD
    assocation_signal_handler(traitement_SIGCHLD,SIGCHLD);
    sigset_t* mask = malloc(sizeof(sigset_t));

    //Traitement de SIGINT et SIGTSTP
    switch (choix_traitement_signal){

        case 1 : //Traitement de SIGINT et SIGTSTP avec les handlers codés précedemment
                assocation_signal_handler(traitement_SIGINT,SIGINT);
                assocation_signal_handler(traitement_SIGTSTP,SIGTSTP);
                break;
        case 2 : //Association de SIGINT et SITSTP à SIG_ING avec signal pour ignorer crtl c et crt z
                signal(SIGINT,SIG_IGN);
                signal(SIGTSTP,SIG_IGN);
                break;

        case 3 : //Masquage des signaux SIGINT et SITSTP               
                sigemptyset(mask);
                sigaddset(mask,SIGINT); // ajout du signal de la liste des siganux masqués
                sigaddset(mask,SIGTSTP); // ajout du signal de la liste des siganux masqués
                sigprocmask(SIG_SETMASK,mask, NULL);
                break;
                }
}

/* Gestion redirection des entrées/sorties d’un processus */
void traitement_redirection(const char *out, const char *in){
    if(out != NULL){
        int desc = open(out,O_WRONLY | O_TRUNC | O_CREAT,0640);
        //Redurection du descripteur 1 (écran)
        dup2(desc,1);
        close(desc);
    }
    if(in != NULL){
        int src = open(in,O_RDONLY);
        //Redirection du descripteur 0 (clavier)
        dup2(src,0);
        close(src);
    } 
}


/* Fonction exécutant la commande écrite en ligne de commande
 * Entrées : cmd : Un tableau de String qui contient toutes les chaines de carctères de la ligne de commandes
 *           background : Booléen indiquant si la tâche s'exécute en arrière plan ou non
 */
void exe_commandes(char** cmd, bool background,const char *out, const char *in){
 
    pid_t retour;
    
    retour= fork();

    if(retour == -1){
        printf("Erreur fork\n");
        exit(1);
    }
    if(retour == 0){ // processus fils

        //Traitement des signaux SIGINT et SIGSTP
        if(choix_traitement_signal == 2){
            //Association de SIGINT et SITSTP à SIG_DFL pour tenir compte crtl c et crt z
            signal(SIGINT,SIG_DFL);
            signal(SIGTSTP,SIG_DFL);
        }
        if (!background){
            if(choix_traitement_signal == 3){
                //Demasquage de SIGINT et SIGTSP
                sigset_t* mask = malloc(sizeof(sigset_t));
                sigemptyset(mask);
                sigdelset(mask,SIGINT); // ajout du signal de la liste des siganux masqués
                sigdelset(mask,SIGTSTP); // ajout du signal de la liste des siganux masqués
                sigprocmask(SIG_SETMASK,mask, NULL);
            }
        }
        else{
            //Détache le processus fils en arrière plan (évite poropagation Ctrl C)
            setpgid(0,0);
        }

        //Traiter la redirection des entrées/sorties
        traitement_redirection(out,in);

        //Traitement de la commande
        retour=execvp(cmd[0],cmd);
		
		//Gestion d'erreur
        if(retour==-1){
            exit(-1);
            printf("Le fork n' pas focntioné, fork vaut -1");
        }
    }
    else{
        //Processus père
        //Lance une commande si elle a un & en fin
        if (!background){
            cmd_avant_plan = retour;
            while(cmd_avant_plan >0){
                pause();
            }
        }
    }
}

/* Fonction exécutant la commande écrite en ligne de commande lors de l'utilisation de pipes */
void exe_commandes_pipe(char** cmd, bool background){
     //Traitement des signaux SIGINT et SIGSTP
        if(choix_traitement_signal == 2){
            //Association de SIGINT et SITSTP à SIG_DFL pour tenir compte crtl c et crt z
            signal(SIGINT,SIG_DFL);
            signal(SIGTSTP,SIG_DFL);
        }
        if (!background){
            if(choix_traitement_signal == 3){
                //Demasquage de SIGINT et SIGTSP
                sigset_t* mask = malloc(sizeof(sigset_t));
                sigemptyset(mask);
                sigdelset(mask,SIGINT); // ajout du signal de la liste des siganux masqués
                sigdelset(mask,SIGTSTP); // ajout du signal de la liste des siganux masqués
                sigprocmask(SIG_SETMASK,mask, NULL);
            }
        }

        else{
            //Détache le processus fils en arrière plan (évite poropagation Ctrl C)
            setpgid(0,0);
        }

        //Traitement de la commande
        int retour=execvp(cmd[0],cmd);
		
		//Gestion d'erreur
        if(retour==-1){
            exit(-1);
            printf("Le fork n' pas focntioné, fork vaut -1");
        }
}


/* Fonction fermant les pipes nécessaires et modifiant les sorties et entéres de la commande avant son exécution */
void gestion_pipes(struct cmdline *commandes,bool background,int nb_cmd,int indexseq,int pipes[nb_cmd-1][2]){
    pid_t pid;
    bool fin = indexseq == nb_cmd-1;

        pid = fork();
        if(pid == -1){
            perror("Erreur fork");
            exit(3);
        }
        if(pid == 0){//le fils
            for(int i = 0; i< nb_cmd-1; i++){
                if(i != indexseq && i != indexseq -1){
                    close(pipes[i][1]);
                    close(pipes[i][0]);
                }
            }
            if(!fin){//La commande n'est pas la dernière de la ligne de commandes
                dup2(pipes[indexseq][1],STDOUT_FILENO);
                close(pipes[indexseq][1]);
                close(pipes[indexseq][0]);
            }
            if(indexseq >0){//La commande n'est pas la première de la ligne de commandes
                dup2(pipes[indexseq-1][0],STDIN_FILENO);
                close(pipes[indexseq-1][1]);
                close(pipes[indexseq-1][0]);
            }

            exe_commandes_pipe(commandes->seq[indexseq],background);
        }
        else{//père
        
            //Ne fonctionne pas
            /*if(!background){
                cmd_avant_plan = pid;
                while(cmd_avant_plan >0){
                    wait(NULL);
                }
            }*/
        }
}
    

int main(void) {
    bool fini= false;

    traitement_signaux();

    while (!fini) {
        printf("> ");
        struct cmdline *commande= readcmd();

        if (commande == NULL) {
            // commande == NULL -> erreur readcmd()
            perror("erreur lecture commande \n");
            exit(EXIT_FAILURE);
    
        } else {

            if (commande->err) {
                // commande->err != NULL -> commande->seq == NULL
                printf("erreur saisie de la commande : %s\n", commande->err);
        
            } else {

                int indexseq= 0;
                char **cmd;
                int nb_cmd =0;
                //Calul de nombre de commandes de la ligne de commandes
                while ((cmd= commande->seq[nb_cmd])) {
                    nb_cmd++;
                }
                //Ouvrir tous les pipes
                int pipes[nb_cmd-1][2];
                for(int i = 0; i< nb_cmd-1; i++){
                    //Création du pipe
                    if(pipe(pipes[i])== -1){
                        perror("Erreur pipe");
                        exit(2);
                    }
                }
                while ((cmd= commande->seq[indexseq])) {
                    
                    if (cmd[0]) {
                        if (strcmp(cmd[0], "exit") == 0) { // On quitte le minishell
                            fini= true;
                        }
                        else { // On execute une commande autre que quitter
;
                            //Traitement de la commande
                            bool background = commande->backgrounded != NULL;

                            //Disjonction de cas en fonction de la présence d'un pipe 
                            if(nb_cmd > 1){ //Plusieurs commandes donc utilisation de pipes
                                gestion_pipes(commande, background, nb_cmd, indexseq, pipes);
                            }
                            else{
                                exe_commandes(cmd,background,commande->out,commande->in);
                            }
                        }

                        indexseq++;
                    }
                }
                //Ferme les pipes
                for(int i = 0; i< nb_cmd-1; i++){
                    close(pipes[i][0]);
                    close(pipes[i][1]);
                }
            }
        }
    }
    return EXIT_SUCCESS;
}
