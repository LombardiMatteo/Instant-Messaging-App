#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <signal.h>

// Lista comandi
#define CMD_SIGNUP "SIGNUP\0"
#define CMD_IN "IN\0"
#define CMD_SHOW "SHOW\0"
#define CMD_HANGING "HANGING\0"
#define CMD_CHAT "CHAT\0"
#define CMD_OUT "OUT\0"
#define CMD_GRPCHAT "GRPCHAT\0"

// Lista espressioni
#define OK "OK\0"
#define NOT_OK "NOT_OK\0"
#define READY "READY\0"
#define NOTIFY "NOTIFY\0"
#define PENDING "PENDING\0"
#define NORMAL "NORMAL\0"
#define GROUP "GROUP\0"
#define CHAT_EXIT "CHAT_EXIT\0"
#define USERS_ONLINE "USERS_ONLINE\0"
#define SERVER_OFF "SERVER_OFF\0"
#define UPDATE_SD "UPDATE_SD\0"

// Lista comandi chat
#define QUIT "\\q\0"
#define USER_ADD "\\u\0"
#define ADD "\\a\0"
#define SHARE "\\share\0"

#define STDIN 0
#define MAX_USERS 10

// Struttura dati che descrive un utente
struct user {
    int id;
    char username[1024];
    int port;
    struct sockaddr_in addr;
};

// Struttura dati per la gestione degli utenti in una chat
struct usr_chat {
    char username[1024];
    int port;
    int sd;
};

struct user this_usr;
struct usr_chat users[MAX_USERS];

struct sockaddr_in dev_addr;
struct sockaddr_in srv_addr;
int srv_sd;
int srv_port;

int n_users;

int listening_socket;

fd_set master;
fd_set read_fds;
int fdmax;

bool srv_off;

// ****************************************************
// *                FUNZIONI GENERALI                 *
// ****************************************************

// Funzione che azzera ed inizializza i set per la select
void fdtInit() {

    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    // Aggiungo il descrittore dello STDIN al set master
    FD_SET(STDIN, &master);

    fdmax = 0;

};

// Funzione che controlla la presenza di una stringa all'interno di un file
bool check_stringa (FILE *fp, const char *string) {
    
    char buffer[1024];

    while (fscanf(fp, "%s", buffer) == 1){
        if (!strcmp(buffer, string))
            return true;
    }

    return false;

}

// Funzione per l'invio di un comando (solo per migliore leggibilità)
void sendCommand(int sd, char* cmd) {
    send(sd, cmd, strlen(cmd)+1, 0);
}

// Funzione che trasforma un timestamp in una stringa
void timestampTranslate(time_t ts, char *string) {

    struct tm *t;
    t = localtime(&ts);
    strftime(string, 1024, "%d-%B-%Y|%H:%M", t);

}

void printStartCommands() {

    printf("- Per registrarti digita: signup\n");
    printf("- Per effettuare l'accesso digita: in\n\n");

}

void printOnlineCommands() {

    printf("\nChe cosa vuoi fare?\n");
    printf("- Per ricevere la lista degli utenti che ti hanno inviato messaggi digita: hanging\n");
    printf("- Per visualizzare i messaggi ricevuti da qualcuno digita: show\n");
    printf("- Per iniziare a una chat digita: chat\n");
    printf("- Per uscire dall'applicazione digita: out\n\n");

}

void printChatCommands() {

    printf("\n- Per inviare un messaggio scrivi il testo premi invio\n");
    printf("- Per aggiungere un utente alla chat digita: \\u\n");
    printf("- Per condividere un file digita: \\share\n");
    printf("- Per uscire dalla chat digita: \\q\n\n");

}

// Funzione che crea un socket di ascolto
void listeningSocketCreate () {

    listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    
    if (listening_socket == -1) {
        printf("ERRORE: Creazione socket non riuscita\n");
        exit(-1);
    }    

    memset(&this_usr.addr, 0, sizeof(this_usr.addr)); // pulizia
    this_usr.addr.sin_family = AF_INET;
    this_usr.addr.sin_port = htons(this_usr.port);
    this_usr.addr.sin_addr.s_addr = INADDR_ANY;

    if (setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        printf("ATTENZIONE: setsockopt() non riuscita");
    }

    if (bind(listening_socket, (struct sockaddr*)&this_usr.addr, sizeof(this_usr.addr)) == -1) {
        printf("ERRORE: bind() non riuscita\n");
        exit(-1);
    }

}

// Funzione che si occupa della connessione del device al Server
void serverConnect() {

    printf("\nConnessione al Server in corso...\n");

    char buffer[1024];
    int ret;

    srv_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sd == -1) {
        printf("ERRORE: Creazione socket non riuscita\n");
        exit(-1);
    }
    
    while(1) {
        
        printf("Inserire la porta del Server al quale si vuole accedere:\n -> ");
        scanf("%s", buffer);
        srv_port = atoi(buffer);

        memset(&srv_addr, 0, sizeof(srv_addr)); // pulizia
        srv_addr.sin_family = AF_INET;
        srv_addr.sin_port = htons(srv_port);

        if (setsockopt(srv_sd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
            printf("ATTENZIONE: setsockopt() non riuscita");
        }

        inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);

        ret = connect(srv_sd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));

        if(ret < 0) {
            printf("\nATTENZIONE! Connessione al Server sulla porta %d non riuscita.\n\n", srv_port);
            printf("Riprovare...\n(La porta di default è la 4242)\n\n");
            continue;
        }
        
        printf("\nConnessione al Server sulla porta %d riuscita.\n", srv_port);
        break;
    }

}

// Funzione che prova a riconnettere l'utente al server (serve nel caso in cui il server si disconnetta e si riconnetta senza che l'utente si diconnetta mentre il server è OFF)
bool serverReconnect() {

    int ret;

    srv_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_sd == -1) {
        printf("ERRORE: Creazione socket non riuscita\n");
        exit(-1);
    }
    
    memset(&srv_addr, 0, sizeof(srv_addr)); // pulizia
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(srv_port);

    setsockopt(srv_sd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

    inet_pton(AF_INET, "127.0.0.1", &srv_addr.sin_addr);

    ret = connect(srv_sd, (struct sockaddr*)&srv_addr, sizeof(srv_addr));

    if(ret < 0) {
        return false;
    }
    else {
        return true;
    }

}

// Funzione che crea un socket per la connessione ad un altro device
int socketCreate(int port) {

    int sd;

    sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        return -1;
    }

    memset(&dev_addr, 0, sizeof(dev_addr)); // pulizia
    dev_addr.sin_family = AF_INET;
    dev_addr.sin_port = htons(port);

    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        printf("ATTENZIONE: setsockopt() non riuscita");
    }

    inet_pton(AF_INET, "127.0.0.1", &dev_addr.sin_addr);

    return sd;        

}

// Funzione che si occupa della configurazione del device quando diventa online
void onlineSetup() {

    printf("\nCaricamento...\n");

    char buffer[1024];

    // Invio la porta al server
    sprintf(buffer, "%d", this_usr.port);
    send(srv_sd, buffer, strlen(buffer)+1, 0);

    // Ricevo l'id che mi è stato assegnato dal server
    recv(srv_sd, buffer, sizeof(buffer), 0);
    this_usr.id = atoi(buffer);

    recv(srv_sd, buffer, sizeof(buffer), 0);
    if(!strcmp(buffer, OK)) {
        printf("\nCiao %s!\n", this_usr.username);
    }
    else {
        printf("\nERRORE: Caricamento non riuscito.\n");
        exit(-1);
    }

    sendCommand(srv_sd, READY);

    // Ricevo una stringa dal server
    recv(srv_sd, buffer, sizeof(buffer), 0);

    if(!strcmp(buffer, NOTIFY)) {       // Qualcuno ha letto i messaggi
        while(1) {
            sendCommand(srv_sd, READY);
            // Ricevo i nomi di chi ha letto i messaggi
            recv(srv_sd, buffer, sizeof(buffer), 0);
            if(!strcmp(buffer, OK)) {
                break;
            }
            // Stampo la notifica a video
            printf("\nNOTIFICA: %s ha letto i tuoi messaggi.\n", buffer);       
        }   
    }

}

// Funzione che compone il messaggio da inviare
void msgCompose(const char *text, char *message) {

    char timestamp[1024];
    time_t rawtime;

    time(&rawtime);
    timestampTranslate(rawtime, timestamp);

    strcpy(message, "* [");
    strcat(message, this_usr.username);
    strcat(message, "] [");
    strcat(message, timestamp);
    strcat(message, "] ");
    strcat(message, text);
    strcat(message, "\n");

}


// ******************************************
// *                COMANDI                 *
// ******************************************

void signup() {

    char buffer[1024];
    char usr[1024];
    char pwd[1024];

    printf("\nRegistrazione in corso...\n\n");
    
    sendCommand(srv_sd, CMD_SIGNUP);
    
    while(1) {
        
        printf("Inserire uno username:\n");
        
        scanf("%s", usr);

        // Invio lo username al Server che controllerà che sia univoco
        send(srv_sd, usr, strlen(usr)+1, 0);

        // Ricevo la risposta dal Server
        recv(srv_sd, buffer, sizeof(buffer), 0);

        if (!strcmp(buffer, OK)) {
            strcpy(this_usr.username, usr);
            break;
        }

        printf("\nATTENZIONE: username inserito gia' in uso.\n\n");

    }

    printf("\nOK! Username valido.\n\n");

    while(1) {

        printf("Inserire una password:\n");

        scanf("%s", pwd);

        if(strlen(pwd) < 5) {
            printf("\nATTENZIONE: password inserita non valida. La password deve contenere almeno 5 caratteri\n\n");
            continue;
        }

        break;

    }

    printf("\nOK! Password valida.\n");

    strcpy(buffer, usr);
    strcat(buffer, pwd);
    send(srv_sd, buffer, strlen(buffer)+1, 0);

    // Aspetto l'OK del Server
    recv(srv_sd, buffer, sizeof(buffer), 0);
    
    if(strcmp(buffer, OK)) {
        printf("\nERRORE: Qualcosa è andato storto.\n");
        exit(-1);
    }

    // Creo la cartella per il log delle chat
    mkdir(usr, 0700);

    printf("\nRegistrazione avvenuta con successo.\n\n");

    onlineSetup();

}

void in() {
    
    char buffer[1024];
    char usr[1024];
    char pwd[1024];

    sendCommand(srv_sd, CMD_IN);

    printf("\nAccesso in corso...\n");

    while(1) {

        printf("\nInserire username:\n");
        scanf("%s", usr);
        
        // Invio lo username al Server che controllerà che sia esistente 
        send(srv_sd, usr, strlen(usr)+1, 0);

        // Attendo l'OK dal Server
        recv(srv_sd, buffer, sizeof(buffer), 0);

        if(strcmp(buffer, OK)) {
            printf("\nATTENZIONE: username inserito non esistente.\n");
            continue;
        }      

        strcpy(this_usr.username, usr);

        break;

    }

    while(1) {

        printf("\nInserire password:\n");
        scanf("%s", pwd);

        // Compongo la stringa "usernamepassword" e la invio al Server per il controllo
        strcpy(buffer, usr);
        strcat(buffer, pwd);

        send(srv_sd, buffer, strlen(buffer)+1, 0);

        // Attendo l'OK dal Server
        recv(srv_sd, buffer, sizeof(buffer), 0);

        if(strcmp(buffer, OK)) {
            printf("\nATTENZIONE: password inserita non corretta.\n");
            continue;
        }

        break;
        
    }

    printf("\nAccesso avvenuto correttamente.\n");

    onlineSetup();

}

void hanging() {

    int count;          // variabile che mi indica se ho ricevuto messaggi pendenti
    char buffer[1024];

    sendCommand(srv_sd, CMD_HANGING);

    // Mando l'id al Server
    sprintf(buffer, "%d", this_usr.id);
    send(srv_sd, buffer, strlen(buffer)+1, 0);

    count = 0;

    printf("\nUtenti che hanno inviato messaggi mentre eri offline:\n");

    while(1) {

        // Ricevo la stringa dal Server
        recv(srv_sd, buffer, sizeof(buffer), 0);

        if(!strcmp(buffer, OK)) // Se ricevo la striga di OK ho finito
            break;

        count++;
        // Stampo la stringa "username num_pending last_timestamp"
        printf("%s\n", buffer);

        sendCommand(srv_sd, READY);

    }

    if(!count) {        // Se count è rimasta a 0 non ho ricevuto messaggi 
        printf("Nessun messaggio ricevuto.\n");
    }

}

void show() {

    char usr[1024];
    char buffer[1024];
    char path[1024];
    FILE *fptr;

    sendCommand(srv_sd, CMD_SHOW);

    // Mando l'id al Server
    sprintf(buffer, "%d", this_usr.id);
    send(srv_sd, buffer, strlen(buffer)+1, 0);

    printf("\nInserisci lo username dell'utente del quale vuoi leggere i messaggi:\n");
    scanf("%s", usr);

    while(1) {
        recv(srv_sd, buffer, sizeof(buffer), 0);
            if(strcmp(buffer, READY)){
                continue;
            }
        break;
    }
    
    // Invio lo username dell'utente del quale voglio leggere i messaggi
    send(srv_sd, usr, strlen(usr)+1, 0);
    
    recv(srv_sd, buffer, sizeof(buffer), 0);
    if(!strcmp(buffer, NOT_OK)) {
        printf("\nATTENZIONE: username inserito insesitente o non ci sono messaggi pendenti da parte di questo utente.\n");
        return;
    }

    strcpy(path, this_usr.username);
    strcat(path, "/");
    strcat(path, usr);
    strcat(path, ".txt");

    printf("\nEcco i messaggi che %s ti ha inviato mentre eri offline:\n\n", usr);

    while(1) {
        
        sendCommand(srv_sd, READY);

        // Ricevo il messaggio
        recv(srv_sd, buffer, sizeof(buffer), 0);

        if(!strcmp(buffer, OK)) {       // Non ho più messaggi da ricevere
            break;
        }

        // Stampo il messaggio a video
        printf("*%s\n", buffer);

        // Salvo il messaggio nel file che contiene la cronologia della chat
        fptr = fopen(path, "a");
        fprintf(fptr, "*%s", buffer);
        fclose(fptr);

    }
}

void out() {

    char buffer[1024];
    
    printf("\nChiusura in corso ...\n\n");

    sendCommand(srv_sd, CMD_OUT);

    while(1) {

        recv(srv_sd, buffer, sizeof(buffer), 0);
        if(strcmp(buffer, READY)){
            continue;
        }
        break;

    }

    // Mando l'id al Server
    sprintf(buffer, "%d", this_usr.id);
    send(srv_sd, buffer, strlen(buffer)+1, 0);

    // Chiudo i Socket
    close(srv_sd);
    close(listening_socket);

    exit(0);

}

void chatOffline() {
    
    char buffer[1024];
    char text[1024];
    char msg[1024];

    printf("\nPer inviare un messaggio scrivi il testo e premi il tasto invio\n");
    printf("Per uscire dalla chat digita '\\q' e premi il tasto invio\n\n");

    while(1) {
        
        scanf("%s", buffer);

        if(strcmp(buffer, QUIT)) {
            
            // Scrivo il testo del messaggio in una stringa
            strcpy(text, buffer);
            fgets(buffer, sizeof(buffer), stdin);
            buffer[strlen(buffer)-1] = '\0';
            strcat(text, buffer);

            // Preparo il messaggio da inviare (formato corretto)
            msgCompose(text, msg);

            // Invio il messaggio al server
            send(srv_sd, msg, strlen(msg)+1, 0);

            // Attendo l'OK di avvenuta ricezione
            recv(srv_sd, buffer, sizeof(buffer), 0);
            if(strcmp(buffer, OK)) {
                printf("ATTENZIONE: il messaggio non è stato memorizzato.");
            } else {
                printf("\n%s\n", msg);
            }

            continue;

        }
        
        sendCommand(srv_sd, QUIT);

        break;
    }
    
}

int grpChatSetup(char new_usr[1024]) {

    int ret;
    char buffer[1024];
    int new_usr_port;
    int new_usr_sd;
    int i;

    sendCommand(srv_sd, USER_ADD);

    // Mi sincronizzo con il server
    while(1) {
        recv(srv_sd, buffer, sizeof(buffer), 0);
        if(strcmp(buffer, READY)){
            continue;
        }
        break;
    }
    
    // Invio il nome dell'utente al server
    send(srv_sd, new_usr, strlen(new_usr)+1, 0);

    // Ricevo il numero di porta dell'utente da aggiungere
    recv(srv_sd, buffer, sizeof(buffer), 0);
    new_usr_port = atoi(buffer);
    
    // Mi connetto al device con il quale voglio chattare
    printf("\nConnessione con %s in corso...\n", new_usr);

    new_usr_sd = socketCreate(new_usr_port);
    if(new_usr_sd == -1) {
        printf("ERRORE: Creazione socket non riuscita.\n");
        return -1;
    }

    ret = connect(new_usr_sd, (struct sockaddr*)&dev_addr, sizeof(dev_addr));
    if(ret < 0) {
        printf("ERRORE: Connessione con %s non riuscita.\n", new_usr);
        return -1;
    }
    
    // Mando la richiesta di inizio chat a usr_dest
    sendCommand(new_usr_sd, CMD_GRPCHAT);

    // Aspetto che new_usr sia pronto
    while(1) {
        recv(new_usr_sd, buffer, sizeof(buffer), 0);
        if(!strcmp(buffer, READY)) {
            break;
        }
    }

    // Invio a new_usr il mio username
    send(new_usr_sd, this_usr.username, strlen(this_usr.username)+1, 0);

    // Ricevo l'OK da new_usr
    recv(new_usr_sd, buffer, sizeof(buffer), 0);
    if(strcmp(buffer, OK)) {
        printf("ERRORE: non è stato possibile aggiungere %s alla chat.\n", new_usr);
        close(new_usr_sd);
        sendCommand(srv_sd, NOT_OK);
        return -1;
    }

    //Mando l'OK al server dell'aggiumta alla chat dell'utente
    sendCommand(srv_sd, OK);

    // Invio la mia porta a new_usr
    sprintf(buffer, "%d", this_usr.port);
    send(new_usr_sd, buffer, sizeof(buffer), 0);

    while(1) {
        recv(new_usr_sd, buffer, sizeof(buffer), 0);
        if(!strcmp(buffer, READY)) {
            break;
        }
    }
    
    // Invio le informazioni degli altri utenti nella chat new_usr
    for(i = 0; i < n_users; i++) {
        send(new_usr_sd, users[i].username, strlen(users[i].username)+1, 0);
        while(1) {
            recv(new_usr_sd, buffer, sizeof(buffer), 0);
            if(!strcmp(buffer, READY)) {
                break;
            }
        }
        sprintf(buffer, "%d", users[i].port);
        send(new_usr_sd, buffer, sizeof(buffer), 0);
        while(1) {
            recv(new_usr_sd, buffer, sizeof(buffer), 0);
            if(!strcmp(buffer, READY)) {
                break;
            }
        }
    }
    sendCommand(new_usr_sd, OK);

    // Aggiorno la struttura dati per la chat
    strcpy(users[n_users].username, new_usr);
    users[n_users].port = new_usr_port;
    users[n_users].sd = new_usr_sd;
    n_users++;

    return new_usr_sd;

}

void chatOnline(int sd, char usr_dest[1024], char *type) {

    fd_set chat_master;
    fd_set chat_read;
    int chat_fdmax;
    char buffer[1024];
    char string[1024];
    char text[1024];
    char msg_in[1024];
    char msg_out[1024];
    char path[1024];
    char timestamp[1024];
    char file_name[1024];
    char file_path[1024];
    int new_sd;
    int ret;
    int i, j;
    int count;
    int addrlen;
    time_t rawtime;
    FILE *fptr;

    // Sistemo i set per la select
    FD_ZERO(&chat_master);
    FD_ZERO(&chat_read);
    FD_SET(STDIN, &chat_master);
    FD_SET(srv_sd, &chat_master);
    FD_SET(listening_socket, &chat_master);
    FD_SET(sd, &chat_master);

    chat_fdmax = srv_sd;
    if(listening_socket > srv_sd) {
        chat_fdmax = listening_socket;
    }
    if(sd > chat_fdmax) {
        chat_fdmax = sd;
    }

    if(!strcmp(type, GROUP)) {

        printf("\n%s ti ha aggiunto alla chat di gruppo.\n", users[0].username);

        strcpy(string, users[0].username);

        // Mi connetto con gli utenti della chat
        for(j = 1; j < n_users; j++) {

            new_sd = socketCreate(users[j].port);
            if(new_sd == -1) {
                printf("ERRORE: Creazione socket non riuscita.\n");
                j--;
                continue;
            }

            ret = connect(new_sd, (struct sockaddr*)&dev_addr, sizeof(dev_addr));
            if(ret < 0) {
                printf("ERRORE: Connessione con %s non riuscita.\n", users[j].username);
                j--;
                continue;
            }

            users[j].sd = new_sd;

            // Invio le mie informazioni all'utente con il quale mi connetto
            send(new_sd, this_usr.username, strlen(this_usr.username)+1, 0);
            while(1) {
                recv(new_sd, buffer, sizeof(buffer), 0);
                if(!strcmp(buffer, READY)) {
                    break;
                }
            }
            sprintf(buffer, "%d", this_usr.port);
            send(new_sd, buffer, strlen(buffer)+1, 0);

            // Aggiungo il nuovo socket al set
            FD_SET(new_sd, &chat_master);
            if(new_sd > chat_fdmax) {
                chat_fdmax = new_sd;
            }

            strcat(string, ", ");
            strcat(string, users[j].username);

        }

        printf("\nUtenti nella chat: tu, %s.\n", string);

    }

    printChatCommands();

    while(1) {

        chat_read = chat_master;
        select(chat_fdmax+1, &chat_read, NULL, NULL, NULL);

        for(i = 0; i <= chat_fdmax; i++) {
            if(FD_ISSET(i, &chat_read)) {
                if(i == 0) {
                    // Pulisco le variabili msg_in e msg_out
                    memset(msg_in, 0, sizeof(msg_in));
                    memset(msg_out, 0, sizeof(msg_out));
                    
                    scanf("%s", buffer);

                    if(!strcmp(buffer, QUIT)) {

                        // Avverto gli altri utenti
                        for(j = 0; j < n_users; j++) {
                            sendCommand(users[j].sd, QUIT);
                            close(users[j].sd);
                        }

                        if(srv_off) {

                            if(serverReconnect()) {
                                
                                srv_off = false;

                                // Il server deve aggiornare l'sd
                                sendCommand(srv_sd, UPDATE_SD);

                                while(1) {
                                    recv(srv_sd, buffer, sizeof(buffer), 0);
                                    if(strcmp(buffer, READY)) {
                                        continue;
                                    }
                                    break;
                                }

                                // Invio l'id al server
                                sprintf(buffer, "%d", this_usr.id);
                                send(srv_sd, buffer, strlen(buffer)+1, 0);

                                // Ricevo l'OK dal server
                                recv(srv_sd, buffer, sizeof(buffer), 0);
                                if(strcmp(buffer, OK)) {
                                    printf("\nERRORE: Qualcosa è andato storto.\n");
                                    exit(-1);
                                }

                                sendCommand(srv_sd, CHAT_EXIT);

                                // Sistemo i dati
                                n_users = 0;
                                memset(users, 0, sizeof(users));

                                printf("\nNOTIFICA: Il server e' tornato online.\n");
                                printf("Puoi continuare ad usare l'applicazione\n");

                                FD_SET(srv_sd, &master);
                                if(srv_sd > fdmax) {
                                    fdmax = srv_sd;
                                }
                                
                                return;

                            }

                            printf("\nATTENZIONE: Server offline. Non è più possibile utilizzare l'applicazione.\n");
                            printf("\nChiusura in corso ...\n");

                            // Salvo timestamp di logout
                            time(&rawtime);
                            timestampTranslate(rawtime, timestamp);
                            strcpy(path, this_usr.username);
                            strcat(path, "/logout.txt");
                            fptr = fopen(path, "w");
                            fprintf(fptr, "%s", timestamp);

                            exit(0);

                        }

                        // Avverto il server che esco dalla chat
                        sendCommand(srv_sd, CHAT_EXIT);

                        // Sistemo i dati
                        n_users = 0;
                        memset(users, 0, sizeof(users));

                        return;                     
                    }

                    if(!strcmp(buffer, USER_ADD)) {

                        if(srv_off) {
                            printf("\nATTENZIONE: Il server è offline. Puoi solo inviare/ricevere messaggi e file.\n\n");
                            break;
                        }

                        sendCommand(srv_sd, USERS_ONLINE);
                        
                        strcpy(path, this_usr.username);
                        strcat(path, "/users_online.txt");
                        fptr = fopen(path, "w");
                        count = 0;

                        printf("\nUtenti online che puoi aggiungere alla chat:\n");
                        while(1) {
                            recv(srv_sd, buffer, sizeof(buffer), 0);
                            if(!strcmp(buffer, OK)) {
                                break;
                            }
                            count++;
                            fprintf(fptr, "%s\n", buffer);
                            printf("%s\n", buffer);
                            sendCommand(srv_sd, READY);
                        }

                        fclose(fptr);

                        if(count == 0) {
                            printf("\nATTENZIONE: nessun utente puo' essere aggiunto alla chat in questo momento.\n");
                            printChatCommands();
                            break;
                        }

                        printf("\nPer aggiungere un utente alla chat digita: \\a\n");
                        printf("Altrimenti digita qualsiasi cosa per tornare alla chat\n");

                        scanf("%s", buffer);
                        if(!strcmp(buffer, ADD)) {

                            fptr = fopen(path, "r");

                            printf("\nChi vuoi aggiungere?\n");

                            scanf("%s", buffer);
                            if(check_stringa(fptr, buffer)) {
                                new_sd = grpChatSetup(buffer);
                                if(new_sd > 0) {
                                    printf("\nNOTIFICA: %s e' entrato nella chat.\n\n", buffer);
                                    // Aggiungo il nuovo sd al master
                                    FD_SET(new_sd, &chat_master);
                                    if(new_sd > chat_fdmax) {
                                        chat_fdmax = new_sd;
                                    }
                                }
                            }
                            else {
                                printf("ATTENZIONE: username inserito inesistente, o utente non attualmente contattabile.\n");
                                printf("Nessun utente aggiunto alla chat.\n");
                                printChatCommands();      
                            }
                            
                            fclose(fptr);

                        }
                        else {
                            printChatCommands();
                        }

                        break;
                    }

                    if(!strcmp(buffer, SHARE)) {

                        printf("\nDigita il nome del file che vuoi condividere:\n");
                        scanf("%s", file_name);

                        strcpy(file_path, "files/");
                        strcat(file_path, file_name);

                        fptr = fopen(file_path, "r");
                        if(!fptr) {
                            printf("\nATTENZIONE: il file %s non esiste!\n", file_name);
                            printChatCommands();
                            break;
                        }
                        fclose(fptr);

                        printf("\nCondivisione del file %s in corso...\n", file_name);
                        
                        for(j = 0; j < n_users; j++) {

                            // Avverto gli altri utenti che sto per condividere un file
                            sendCommand(users[j].sd, SHARE);

                            // Attendo che siano pronti
                            while(1) {
                                recv(users[j].sd, buffer, sizeof(buffer), 0);
                                if(!strcmp(buffer, READY)) {
                                    break;
                                }
                            }

                            // Invio il nome del file
                            send(users[j].sd, file_name, strlen(file_name)+1, 0);

                            while(1) {
                                recv(users[j].sd, buffer, sizeof(buffer), 0);
                                if(strcmp(buffer, READY)) {
                                    continue;
                                }
                                break;
                            }

                            fptr = fopen(file_path, "r");

                            // Invio il contenuto del file
                            while(fgets(buffer, sizeof(buffer), fptr)) {
                                send(users[j].sd, buffer, strlen(buffer)+1, 0);
                                while(1) {
                                    recv(users[j].sd, buffer, sizeof(buffer), 0);
                                    if(strcmp(buffer, READY)) {
                                        continue;
                                    }
                                    break;
                                }
                            }
                            // Avverto che ho terminato la condivisione del contenuto
                            sendCommand(users[j].sd, OK);

                            fclose(fptr);

                        }

                        // Se non sono in una chat di gruppo (n_user == 1) salvo il messaggio di condivisione del file nella cronologia
                        if(n_users == 1) {

                            strcpy(path, this_usr.username);
                            strcat(path, "/");
                            strcat(path, users[0].username);
                            strcat(path, ".txt");

                            sprintf(text, "Condiviso il file '%s'.", file_name);
                            msgCompose(text, msg_out);

                            while(1) {
                                recv(users[0].sd, buffer, sizeof(buffer), 0);
                                if(strcmp(buffer, READY)) {
                                    continue;
                                }
                                break;
                            }

                            send(users[0].sd, msg_out, strlen(msg_out)+1, 0);
                            
                            fptr = fopen(path, "a");
                            fprintf(fptr, "*%s", msg_out);
                            fclose(fptr);

                        }

                        printf("\nCondivisione del file terminata.\n");

                        printChatCommands();

                        break;
                    }

                    // Messaggio normale
                    // Scrivo il testo del messaggio in una stringa
                    strcpy(text, buffer);
                    fgets(buffer, sizeof(buffer), stdin);
                    buffer[strlen(buffer)-1] = '\0';
                    strcat(text, buffer);

                    // Preparo il messaggio da inviare (formato corretto)
                    msgCompose(text, msg_out);

                    // Invio il messaggio a tutti gli utenti della chat
                    for(j = 0; j < n_users; j++) {
                        send(users[j].sd, msg_out, strlen(msg_out)+1, 0);
                    }

                    // Se non sono in una chat di gruppo (n_user == 1) salvo il messaggio nella cronologia della chat
                    if(n_users == 1) {

                        strcpy(path, this_usr.username);
                        strcat(path, "/");
                        strcat(path, users[0].username);
                        strcat(path, ".txt");
                        fptr = fopen(path, "a");

                        fprintf(fptr, "*%s", msg_out);

                        fclose(fptr);

                    }

                    // Stampo il messaggio inviato a video
                    printf("\n*%s\n", msg_out);

                }
                else if(i == listening_socket) {        // un nuovo utente si è unito alla chat
                    
                    addrlen = sizeof(dev_addr);

                    new_sd = accept(listening_socket, (struct sockaddr *)&dev_addr, (socklen_t *)&addrlen);
                    
                    // Ricevo le informazioni del nuovo utente e le salvo
                    recv(new_sd, buffer, sizeof(buffer), 0);
                    strcpy(users[n_users].username, buffer);
                    sendCommand(new_sd, READY);
                    recv(new_sd, buffer, sizeof(buffer), 0);
                    users[n_users].port = atoi(buffer);
                    users[n_users].sd = new_sd;

                    n_users++;

                    // Aggiungo il socket al set
                    FD_SET(new_sd, &chat_master);
                    if(new_sd > chat_fdmax) {
                        chat_fdmax = new_sd;
                    }

                    printf("\nNOTIFICA: %s e' entrato nella chat.\n\n", users[n_users-1].username);

                    break;

                }
                else if(i == srv_sd) {          // Vuol dire che il server è andato offline
                    
                    printf("\nATTENZIONE: Il server è offline.\n");
                    printf("Puoi continuare ad inviare/ricevere messaggi e file, ma non sono consentite altre operazioni. Quando uscirai dalla chat verrai disconnesso automaticamente.\n\n");
                    
                    srv_off = true;

                    FD_CLR(i, &chat_master);
                    FD_CLR(i, &master);
                    
                    close(i);

                    break;

                }
                else {

                    ret = recv(i, msg_in, sizeof(msg_in), 0);

                    if(ret > 0) {
                        if(!strcmp(msg_in, QUIT)) {

                            // Cerco l'utente che è uscito dalla chat
                            for(j = 0; j < n_users; j++) {
                                if(users[j].sd == i){
                                    break;
                                }
                            }

                            printf("\nNOTIFICA: %s e' uscito dalla chat.\n", users[j].username);

                            if(n_users == 1) {      // Era l'utimo utente insieme a this_usr nella chat
                                
                                printf("\nNessun utente rimasto nella chat. La chat viene chiusa automaticamente.\n");
                                
                                // Esco dalla chat
                                if(srv_off) {

                                    if(serverReconnect()) {
                                        
                                        srv_off = false;
                                        
                                        // Il server deve aggiornare l'sd
                                        sendCommand(srv_sd, UPDATE_SD);

                                        while(1) {
                                            recv(srv_sd, buffer, sizeof(buffer), 0);
                                            if(strcmp(buffer, READY)) {
                                                continue;
                                            }
                                            break;
                                        }

                                        // Invio l'id al server
                                        sprintf(buffer, "%d", this_usr.id);
                                        send(srv_sd, buffer, strlen(buffer)+1, 0);

                                        // Ricevo l'OK dal server
                                        recv(srv_sd, buffer, sizeof(buffer), 0);
                                        if(strcmp(buffer, OK)) {
                                            printf("\nERRORE: Qualcosa è andato storto.\n");
                                            exit(-1);
                                        }

                                        sendCommand(srv_sd, CHAT_EXIT);

                                        // Sistemo i dati
                                        n_users = 0;
                                        memset(users, 0, sizeof(users));

                                        close(i);

                                        printf("\nNOTIFICA: Il server e' tornato online.\n");
                                        printf("Puoi continuare ad usare l'applicazione\n");

                                        FD_SET(srv_sd, &master);
                                        if(srv_sd > fdmax) {
                                            fdmax = srv_sd;
                                        }

                                        return;

                                    }

                                    printf("\nATTENZIONE: Server offline. Non è più possibile utilizzare l'applicazione.\n");
                                    printf("\nChiusura in corso ...\n");

                                    // Salvo timestamp di logout
                                    time(&rawtime);
                                    timestampTranslate(rawtime, timestamp);
                                    strcpy(path, this_usr.username);
                                    strcat(path, "/logout.txt");
                                    fptr = fopen(path, "w");
                                    fprintf(fptr, "%s", timestamp);
                                    fclose(fptr);

                                    exit(0);

                                }

                                sendCommand(srv_sd, CHAT_EXIT);

                                // Sistemo i dati
                                n_users = 0;
                                memset(users, 0, sizeof(users));

                                close(i);

                                return;

                            }

                            if(j < n_users-1) {
                                // Sistemo la struttura dati della chat
                                strcpy(users[j].username, users[n_users-1].username);
                                users[j].port = users[n_users-1].port;
                                users[j].sd = users[n_users-1].sd;
                            }

                            FD_CLR(i, &chat_master);
                            close(i);

                            n_users--;

                            if(n_users == 1) {
                                printf("ATTENZIONE: Adesso siete rimasti in due nella chat, quindi diventa una chat \"normale\".\n\n");
                            }
                            
                            break;

                        }
                        else if (!strcmp(msg_in, SHARE)){
                            
                            sendCommand(i, READY);
                            
                            for(j = 0; j < n_users; j++) {
                                if(users[j].sd == i) {
                                    break;
                                }
                            }

                            printf("\n%s sta condividendo un file.\n", users[j].username);
                            printf("\nRicezione del file in corso...\n");

                            // Ricevo il nome del file
                            recv(i, file_name, sizeof(file_name), 0);
                            printf("\nNome file: %s\n", file_name);

                            strcpy(file_path, this_usr.username);
                            strcat(file_path, "/");
                            strcat(file_path, file_name);

                            // Creo un file di nome file_name per salvarci il contenuto
                            fptr = fopen(file_path, "w");

                            printf("Contenuto:\n");

                            sendCommand(i, READY);
                            
                            // Ricevo il contenuto del file
                            while(1) {
                                recv(i, buffer, sizeof(buffer), 0);
                                if(!strcmp(buffer, OK)) {       // Contenuto terminato
                                    break;
                                }
                                // Salvo il contenuto del file e lo stampo a video
                                printf("%s", buffer);
                                fprintf(fptr, "%s", buffer);
                                sendCommand(i, READY);
                            }

                            fclose(fptr);                            

                            // Se non sono in una chat di gruppo (n_user == 1) salvo il messaggio di condivisione del file nella cronologia
                            if(n_users == 1) {

                                strcpy(path, this_usr.username);
                                strcat(path, "/");
                                strcat(path, users[0].username);
                                strcat(path, ".txt");

                                sendCommand(users[0].sd, READY);

                                recv(users[0].sd, buffer, sizeof(buffer), 0);
                                
                                fptr = fopen(path, "a");
                                fprintf(fptr, "*%s", buffer);
                                fclose(fptr);

                            }

                            printf("\n\nRicezione file terminata.\n");

                            printChatCommands();

                            break;

                        }
                        else {

                            // Stampo il messaggio a video;
                            printf("\n*%s\n", msg_in);

                            // Se non sono in una chat di gruppo (n_user == 1) salvo il messaggio nella cronologia della chat
                            if(n_users == 1) {
                                
                                strcpy(path, this_usr.username);
                                strcat(path, "/");
                                strcat(path, users[0].username);
                                strcat(path, ".txt");

                                fptr = fopen(path, "a");

                                fprintf(fptr, "%s", msg_in);

                                fclose(fptr);

                            }

                        }

                    }
                }
            }
        }

    }

}

void chatSteup() {

    char buffer[1024];
    char usr_dest[1024];
    char path[1024];
    int port_dest, sd_dest;
    int ret;
    FILE *fptr;

    sendCommand(srv_sd, CMD_CHAT);

    // Mi sincronizzo con il server
    while(1) {
        recv(srv_sd, buffer, sizeof(buffer), 0);
        if(strcmp(buffer, READY)){
            continue;
        }
        break;
    }

   // Mando l'id al Server
    sprintf(buffer, "%d", this_usr.id);
    send(srv_sd, buffer, strlen(buffer)+1, 0);

    recv(srv_sd, buffer, sizeof(buffer), 0);
    if(strcmp(buffer, OK)){
        printf("\nERRORE: Comunicazione con il server non riuscita.\n");
        return;
    }

    sprintf(path, "rubric/%s.txt", this_usr.username);
    fptr = fopen(path, "r");

    printf("\nRubrica:\n");

    while (fgets(buffer, sizeof(buffer), fptr)) {
        printf("%s", buffer);
    }

    fclose(fptr);

    printf("\nCon chi vuoi iniziare a chattare?\n");
    

    while(1) {

        scanf("%s", usr_dest);

        if(!strcmp(usr_dest, QUIT)) {
            sendCommand(srv_sd, QUIT);
            return;
        }
        
        if(!strcmp(usr_dest, this_usr.username)) {
            printf("\nATTENZIONE: non puoi iniziare una chat con te stesso.\n");
            printf("Inserisci un altro username, oppure digita '\\q' per tornare indietro\n");
            continue;
        }

        fptr = fopen(path, "r");
        if(!check_stringa(fptr, usr_dest)) {
            printf("\nATTENZIONE: lo username inserito non e' presente tra i tuoi contatti in rubrica.");
            printf("Inserisci un altro username, oppure digita '\\q' per tornare indietro\n");
            fclose(fptr);
            continue;
        }
        fclose(fptr);

        // Invio lo username inserito al server
        send(srv_sd, usr_dest, strlen(usr_dest)+1, 0);

        recv(srv_sd, buffer, sizeof(buffer), 0);

        if(!strcmp(buffer, NOT_OK)) {
            printf("\nATTENZIONE: l'utente non e' registrato all'app.\n");
            printf("Inserisci un altro username, oppure digita '\\q' per tornare indietro\n");
            continue;
        }
        else if(!strcmp(buffer, PENDING)) {
            printf("ATTENZIONE: sono presenti messaggi pendenti da parte di questo utente.\n");
            printf("Per poter chattare con lui devi prima leggere i messaggi pendenti (comando 'show').\n");
            return;
        }

        break;

    }

    sendCommand(srv_sd, READY);

    recv(srv_sd, buffer, sizeof(buffer), 0);

    if(!strcmp(buffer, OK)) {       // Utente online e libero

        sendCommand(srv_sd, READY);

        // Si può iniziare una chat diretta con l'utente
        // Ricevo il numero di porta di usr_dest
        recv(srv_sd, buffer, sizeof(buffer), 0);
        port_dest = atoi(buffer);

        // Mi connetto al device con il quale voglio chattare
        printf("\nConnessione con %s in corso...\n", usr_dest);

        sd_dest = socketCreate(port_dest);
        if(sd_dest == -1) {
            printf("ERRORE: Creazione socket non riuscita.\n");
            return;
        }

        ret = connect(sd_dest, (struct sockaddr*)&dev_addr, sizeof(dev_addr));
        if(ret < 0) {
            printf("ERRORE: Connessione con %s non riuscita.\n", usr_dest);
            return;
        }

        // Mando la richiesta di inizio chat a usr_dest
        sendCommand(sd_dest, CMD_CHAT);

        // Aspetto che usr_dest sia pronto
        while(1) {
            recv(sd_dest, buffer, sizeof(buffer), 0);
            if(!strcmp(buffer, READY)) {
                break;
            }
        }

        // Invio a usr_dest il mio username
        send(sd_dest, this_usr.username, strlen(this_usr.username)+1, 0);

        // Ricevo l'OK da usr_dest
        recv(sd_dest, buffer, sizeof(buffer), 0);
        if(strcmp(buffer, OK)) {
            sendCommand(srv_sd, NOT_OK);
            close(sd_dest);
            printf("ERRORE: non è stato possibile iniziare una chat con %s.\n", usr_dest);
            return;
        }

        // Mando l'OK al server
        sendCommand(srv_sd, OK);

        // Invio la mia porta a usr_dest
        sprintf(buffer, "%d", this_usr.port);
        send(sd_dest, buffer, strlen(buffer)+1, 0);
        
        // Aggiorno la struttura dati per la chat
        strcpy(users[n_users].username, usr_dest);
        users[n_users].port = port_dest;
        users[n_users].sd = sd_dest;
        n_users++;
            
        chatOnline(sd_dest, usr_dest, NORMAL);

        return;

    }

    // Utente offline o occupato
    // Si inizia una chat offline
    printf("\nATTENZIONE: %s offline, oppure occupato.\n", usr_dest);
    printf("Puoi comunque inviare messaggi a %s. Questi saranno salvati e poi recapitati al destinatario quando lo vorrà. Una volta che i messaggi saranno stati letti, riceverai una notifica\n", usr_dest);
    chatOffline();

}

// Handler per la gestione di segnali di uscita improvvisa CTRL+C, CTRL+Z
void handler(int sig) {

    int j;
    char path[1024];
    char timestamp[1024];
    time_t rawtime;
    FILE *fptr;

    if(n_users > 0) {       // Sono in una chat online

        // Avverto gli altri utenti
        for(j = 0; j < n_users; j++) {
            sendCommand(users[j].sd, QUIT);
            close(users[j].sd);
        }

        if(srv_off) {

            // Salvo timestamp di logout
            time(&rawtime);
            timestampTranslate(rawtime, timestamp);
            strcpy(path, this_usr.username);
            strcat(path, "/logout.txt");
            fptr = fopen(path, "w");
            fprintf(fptr, "%s", timestamp);

            exit(0);

        }

    }

    out();

}

int main(int argc, char *argv[]) {
    
    int ret, i, addrlen, new_sd;
    char buffer[1024];
    char cmd[1024];
    char path[1024];
    char timestamp[1024];
    char usr_dest[1024];
    int port_dest;
    time_t rawtime;
    FILE *fptr;

    if(argc < 2) {
        printf("ERRORE: nessuna porta inserita.\n Per avviare il device digita: ./dev <porta>\n\n");
        exit(-1);
    }

    printf("\n************************** DEVICE AVVIATO **************************\n\n");
    // assegno la porta al device
    this_usr.port = atoi(argv[1]);

    // Connetto il device alla porta
    listeningSocketCreate();

    listen(listening_socket, 10);

    // Inizializzo i set
    fdtInit();
    
    // Aggiungo il listening_socket al master
    FD_SET(listening_socket, &master);

    // Non ci sono altri socket nel set per cui il listener sara' il maggiore
    fdmax = listening_socket;
    
    // Mi connetto al Server
    serverConnect();

    
    // Attendo comando per la registrazione o l'accesso
    while(1) {

        printStartCommands();

        scanf("%s", buffer);

        if(!strcmp(buffer, "signup") || !strcmp(buffer, "SIGNUP")) {
            signup();
            break;
        }
        else if(!strcmp(buffer, "in") || !strcmp(buffer, "IN")) {
            in();
            break;
        }
        printf("\nATTENZIONE: comando inserito non riconosciuto.\n\n");

    }

    // Device online
    printOnlineCommands();

    //Inizializzo le due variabili globali srv_off e n_users che servono al momento della chat
    srv_off = false;
    n_users = 0;

    FD_SET(srv_sd, &master);
    if(fdmax < srv_sd) {
        fdmax = srv_sd; 
    }

    signal(SIGINT, handler);
    signal(SIGTSTP, handler);
    
    while(1) {

        read_fds = master;
        select(fdmax+1, &read_fds, NULL, NULL, NULL);

        for(i = 0; i < fdmax+1; i++) {
            if(FD_ISSET(i, &read_fds)) {

                if(i == 0) {        // STDIN pronto
                    scanf("%s", cmd);
                    
                    if(!strcmp(cmd, "hanging")){
                        // Gestione comando hanging
                        hanging();
                        sleep(1);
                        printOnlineCommands();
                    }
                    else if(!strcmp(cmd, "show")){
                        // Gestione comando show
                        show();
                        sleep(1);
                        printOnlineCommands();
                    }
                    else if(!strcmp(cmd, "chat")){
                        // Gestione comando chat
                        chatSteup();
                        sleep(1);
                        printOnlineCommands();
                    }
                    else if(!strcmp(cmd, "out")){
                        // Gestione comando out
                        out();
                    }
                    else {
                        printf("\nATTENZIONE: il comando inserito non esiste.\n");
                        sleep(1);
                        printOnlineCommands();
                    }
                }
                else if(i == listening_socket) {         // Socket di ascolto pronto
                    addrlen = sizeof(dev_addr); 
                    new_sd = accept(listening_socket, (struct sockaddr *)&dev_addr, (socklen_t *)&addrlen);         // Creo socket di comunicazione
                    FD_SET(new_sd, &master);
                    if(fdmax < new_sd) {
                        fdmax = new_sd; 
                    }
                }
                else if(i == srv_sd) {
                    printf("\nATTENZIONE: Server offline. Non è più possibile utilizzare l'applicazione.\n");
                    printf("\nChiusura in corso ...\n");

                    // Salvo timestamp di logout
                    time(&rawtime);
                    timestampTranslate(rawtime, timestamp);
                    strcpy(path, this_usr.username);
                    strcat(path, "/logout.txt");
                    fptr = fopen(path, "w");
                    fprintf(fptr, "%s", timestamp);

                    exit(0);
                }
                else {
                    ret = recv(i, buffer, sizeof(buffer), 0);

                    if(!ret) {          // Il socket i è stato chiuso
                        FD_CLR(i, &master);
                        close(i);
                        break;
                    }

                    if(!strcmp(buffer, NOTIFY)) {       // Il server mi notifica che qualcuno ha letto dei messaggi pendenti che avevo inviato
                        sendCommand(i, READY);
                        recv(i, buffer, sizeof(buffer), 0);
                        printf("\nNOTIFICA: %s ha letto i messaggi pendenti.\n", buffer);
                        close(i);
                        FD_CLR(i, &master);
                        printOnlineCommands();
                    }
                    else if(!strcmp(buffer, CMD_CHAT)) {
                        
                        sendCommand(i, READY);
                        
                        // Ricevo lo username dell'utente che vuole chattare con me
                        recv(i, usr_dest, sizeof(usr_dest), 0);

                        printf("\nNOTIFICA: %s vuole chattare con te!\n", usr_dest);
                        printf("\nDigita 'no' se non vuoi iniziare la chat.\n");
                        printf("Atrimenti digita qualsiasi cosa se vuoi iniziare la chat.\n");

                        scanf("%s", buffer);
                        if(!strcmp(buffer, "no")) {
                            sendCommand(i, NOT_OK);
                            FD_CLR(i, &master);
                            close(i);
                            printOnlineCommands();
                            break;
                        }
                        else {
                            sendCommand(i, OK);
                        }

                        // Ricevo la porta dall'utente
                        recv(i, buffer, sizeof(buffer), 0);
                        port_dest = atoi(buffer);

                        // Aggiorno la struttura dati per la chat
                        strcpy(users[n_users].username, usr_dest);
                        users[n_users].port = port_dest;
                        users[n_users].sd = i;
                        n_users++;
                        
                        chatOnline(i, usr_dest, NORMAL);

                        FD_CLR(i, &master);
                        close(i);
                        
                        sleep(1);
                        printOnlineCommands();
                    }
                    else if(!strcmp(buffer, CMD_GRPCHAT)) {
                        
                        sendCommand(i, READY);
                        
                        // Ricevo lo username dell'utente che mi vuole aggiungere alla chat di gruppo
                        recv(i, usr_dest, sizeof(usr_dest), 0);

                        printf("\nNOTIFICA: %s ti ha invitato in una chat di gruppo!\n", usr_dest);
                        printf("Digita 'no' se non vuoi entrare nella chat.\n");
                        printf("Atrimenti digita qualsiasi cosa se vuoi entrare nella chat.\n");

                        scanf("%s", buffer);
                        if(!strcmp(buffer, "no")) {
                            sendCommand(i, NOT_OK);
                            FD_CLR(i, &master);
                            close(i);
                            printOnlineCommands();
                            break;
                        }
                        else {
                            sendCommand(i, OK);
                        }

                        // Ricevo la porta dall'utente
                        recv(i, buffer, sizeof(buffer), 0);
                        port_dest = atoi(buffer);

                        // Aggiorno la struttura dati per la chat con le informazioni dell'utente che mi ha invitato
                        strcpy(users[n_users].username, usr_dest);
                        users[n_users].port = port_dest;
                        users[n_users].sd = i;
                        n_users++;
                        
                        sendCommand(i, READY);
                        
                        // Ricevo le informazioni degli altri utenti nella chat e le salvo nella struttura dati
                        while(1) {

                            recv(i, buffer, sizeof(buffer), 0);
                            if(!strcmp(buffer, OK)) {
                                break;
                            }
                            strcpy(users[n_users].username, buffer);

                            sendCommand(i, READY);
                            recv(i, buffer, sizeof(buffer), 0);
                            users[n_users].port = atoi(buffer);                           
                            n_users++;

                            sendCommand(i, READY);

                        }

                        chatOnline(i, usr_dest, GROUP);

                        FD_CLR(i, &master);
                        close(i);

                        sleep(1);
                        printOnlineCommands();
                    }
                }

                break;
            }
        }

    }

}