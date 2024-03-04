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
#include <pthread.h>
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


struct stat st = {0};

struct pending_msgs {
    char usr[1024];
    int num;
    char last_ts[1024];
};

struct device{
    int id;
    char username[1024];
    int port;
    bool online;
    bool busy;
    time_t timestamp_login;
    int sd;
    struct pending_msgs pm[MAX_USERS];
    bool read_notify[MAX_USERS];
};

struct device devices[MAX_USERS];

int n_dev;

fd_set master;
fd_set read_fds;
int fdmax;

// ****************************************************
// *                FUNZIONI GENERALI                 *
// ****************************************************

// Funzione che azzera ed inizializza i set per la select
void fdtInit() {

    FD_ZERO(&master);
    FD_ZERO(&read_fds);
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

void printSrvCommands() {
    printf("\nDigita un comando:\n\n");
    printf("1) help --> mostra i dettagli dei comandi\n");
    printf("2) list --> mostra elenco degli utenti connessi\n");
    printf("3) esc --> chiude il server\n\n");
}

// Funzione che crea un socket
int socketCreate(struct sockaddr_in *addr, int port) {

    int sd;
    sd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (sd == -1) {
        perror("ERRORE: Creazione socket non riuscita\n");
        exit(-1);
    }
    
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        perror("ATTENZIONE: setsockopt() non riuscita");
    }
    

    memset(addr, 0, sizeof(*addr)); // pulizia
    (*addr).sin_family = AF_INET;
    (*addr).sin_port = htons(port);
    (*addr).sin_addr.s_addr = INADDR_ANY;

    if (bind(sd, (struct sockaddr*)&(*addr), sizeof((*addr))) == -1) {
        perror("ERRORE: bind() non riuscita\n");
        exit(-1);
    }

    return sd;
}

// Funzione che recupera il vecchio stato del Server
void serverRecover() {
    
    FILE *fptr, *fptr1;
    int i, j;
    char buffer[1024];
    char path[1024];
    char file_nuovo[1024];
    char file_vecchio[1024];
    char timestamp_out[1024];
    char timestamp_in[1024];
    char usr[1024];
    int port;
    
    fptr = fopen("server_recover.txt", "r");
    
    if(!fptr) {     // Primo avvio (nessun dato)
        n_dev = 0;
        return;
    }
    
    fscanf(fptr, "%d", &n_dev);

    for(i = 0; i < n_dev; i++) {
        
        fscanf(fptr, "%d %s %d %u %d %d", &devices[i].id, devices[i].username, &devices[i].port, (unsigned *)&devices[i].timestamp_login, (int *)&devices[i].online, (int *)&devices[i].busy);
        
        for(j = 0; j < n_dev; j++) {
            fscanf(fptr, "%s %d %s", devices[i].pm[j].usr, &devices[i].pm[j].num, devices[i].pm[j].last_ts);
        }

    }

    fclose(fptr);

    for(i = 0; i < n_dev; i++) {
        
        if(devices[i].online) {         // device online al momento della chiusura del server

            strcpy(path, devices[i].username);
            strcat(path, "/logout.txt");

            fptr = fopen(path, "r");

            if(!fptr) {         // Se non esiste il file 'logout.txt' il device è ancora online
                continue;
            }

            // Il device è andato offline, quindi aggiorno i dati in memoria relativi al device

            fscanf(fptr, "%s", buffer);

            fclose(fptr);
            remove(path);

            // Sistemo la struttura dati
            devices[i].online = false;
            devices[i].busy = false;
            devices[i].timestamp_login  = 0;
            devices[i].port = -1;

            // Aggiorno il registro inserendo il timestamp di logout dell'uente
            strcpy(file_vecchio, "server/registro.txt");
            strcpy(file_nuovo, "server/registro1.txt");

            fptr = fopen(file_vecchio, "r");
            fptr1 = fopen(file_nuovo, "a");

    
            while(fscanf(fptr, "%s", usr) == 1) {
        
                if(!strcmp(usr, devices[i].username)) {
                    fscanf(fptr, "%d %s %s", &port, timestamp_in, timestamp_out);
                    fprintf(fptr1, "%s %d %s %s\n", usr, port, timestamp_in, buffer);
                    continue;
                }

                fscanf(fptr, "%d %s %s", &port, timestamp_in, timestamp_out);
                fprintf(fptr1, "%s %d %s %s\n", usr, port, timestamp_in, timestamp_out);

            }

            fclose(fptr);
            fclose(fptr1);

            remove(file_vecchio);
            rename(file_nuovo, "server/registro.txt");

        }        

    }

}

// Funzione per il "setup" dei dati di un device che diventa online
void devOnlineSetup(int sd, int id) {
    
    FILE *fptr, *fptr1;
    char buffer[1024];
    char timestamp_in[1024];
    char timestamp_out[1024];
    char usr[1024];
    int port;
    char file_vecchio[1024];
    char file_nuovo[1024];
    int i;

    // Ricevo la porta del device
    recv(sd, buffer, sizeof(buffer), 0);
    devices[id].port = atoi(buffer);
    
    // Invio al device l'id che gli è stato assegnato
    sprintf(buffer, "%d", id);
    send(sd, buffer, strlen(buffer)+1, 0);

    devices[id].online = true;
    devices[id].busy = false;
    time(&devices[id].timestamp_login);
    devices[id].sd = sd;

    strcpy(usr, devices[id].username);
    port = devices[id].port;
    timestampTranslate(devices[id].timestamp_login, timestamp_in);

    // Aggiorno il registro
    strcpy(file_vecchio, "server/registro.txt");
    strcpy(file_nuovo, "server/registro1.txt");

    fptr = fopen(file_vecchio, "a+");
    fptr1 = fopen(file_nuovo, "a");

    fprintf(fptr1, "%s %d %s 0\n", usr, port, timestamp_in);
    
    while(fscanf(fptr, "%s", usr) == 1) {
        
        if(!strcmp(usr, devices[id].username)) {
            fscanf(fptr, "%d %s %s", &port, timestamp_in, timestamp_out);
            continue;
        }

        fscanf(fptr, "%d %s %s", &port, timestamp_in, timestamp_out);
        fprintf(fptr1, "%s %d %s %s\n", usr, port, timestamp_in, timestamp_out);

    }

    fclose(fptr);
    fclose(fptr1);

    remove(file_vecchio);
    rename(file_nuovo, "server/registro.txt");

    sendCommand(sd, OK);

    while(1) {
        recv(sd, buffer, sizeof(buffer), 0);
        if(strcmp(buffer, READY)){
            continue;
        }
        break;
    }

    // Controllo se qualcuno ha letto messaggi pendenti dell'utente mentre era offline
    for(i = 0; i < n_dev; i++) {
        if(devices[id].read_notify[i]) {
            break;
        }
    }
    if(i < n_dev) {     // Qualcuno ha letto i messaggi, quindi avverto l'utente
        sendCommand(sd, NOTIFY);
        for(i = 0; i < n_dev; i++) {
            if(devices[id].read_notify[i]) {
                while(1) {
                    recv(sd, buffer, sizeof(buffer), 0);
                    if(strcmp(buffer, READY)){
                        continue;
                    }
                    break;
                }
                // Mando all'utente lo username di chi ha letto i messaggi
                send(sd, devices[i].username, strlen(devices[i].username)+1, 0);
                devices[id].read_notify[i] = false;
            }
        }
    }
    
    sendCommand(sd, OK);

}

void notify(int usr_id1, int usr_id2) {

    int sd;
    struct sockaddr_in addr;
    char buffer[1024];

    sd = socket(AF_INET, SOCK_STREAM, 0);
    
    if (sd == -1) {
        printf("ERRORE: Creazione socket non riuscita\n");
        return;
    }
    
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        printf("ATTENZIONE: setsockopt non riuscita");
    }
    
    // creo indirizzo
    memset(&addr, 0, sizeof(addr)); // pulizia
    addr.sin_family = AF_INET;
    addr.sin_port = htons(devices[usr_id2].port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    connect(sd, (struct sockaddr *)&addr, sizeof(addr));

    sendCommand(sd, NOTIFY);

    while(1) {
        recv(sd, buffer, sizeof(buffer), 0);
            if(strcmp(buffer, READY)){
                continue;
            }
        break;
    }

    send(sd, devices[usr_id1].username, strlen(devices[usr_id1].username)+1, 0);

    close(sd);

}

// Funzione che aggiorna l'sd dell'utente (serve nel caso in cui il server si disconnetta e si riconnetta senza che l'utente si diconnetta mentre il server è OFF)
void update_dev_sd(int sd) {

    char buffer[1024];
    int usr_id;

    sendCommand(sd, READY);

    // Ricevo l'id dall'utente
    recv(sd, buffer, sizeof(buffer), 0);
    usr_id = atoi(buffer);

    devices[usr_id].sd = sd;

    // Mando l'OK al srver
    sendCommand(sd, OK);

}

// *************************************************
// *                COMANDI SERVER                 *
// *************************************************

void help() {
    printf("\nComando 'list': mostra l'elenco degli utenti connessi alla rete indicando, per ognuno, username, timstamp di connessione e numero di porta\n\n");
    printf("Comando 'esc': chiude il server; la terminazione del server impedisce il login degli utenti, ma gli utenti già connessi potranno continuare a chattare\n\n");
}

void list() {
    
    int i;
    char ts[22];

    // Controllo se ci sono untenti connessi
    for(i = 0; i < n_dev; i++) {
        if (devices[i].online)
            break;
    }

    if(i == n_dev) {        // nessuno online
        printf("\nNessun utente connesso.\n");
        return;
    }
    
    printf("\nUtenti online:\n");
    for(i = 0; i < n_dev; i++) {
        
        if (devices[i].online) {
            // Coverto il timestamp di login a stringa
            timestampTranslate(devices[i].timestamp_login, ts);

            printf("%s*%s*%d\n", devices[i].username, ts, devices[i].port);
        }

    }

}

void esc() {
    
    FILE *fptr;
    int i, j;

    printf("\nChiusura in corso...\n\n")
    for(i = 0; i < n_dev; i++) {
        if(devices[i].online) {
            sendCommand(devices[i].sd, SERVER_OFF);
        }
    } 

    fptr = fopen("server_recover.txt", "w");

    fprintf(fptr, "%d\n", n_dev);

    // Salvo i dati untili
    for(i = 0; i < n_dev; i++) {
        
        fprintf(fptr, "%d %s %d %u %d %d\n", devices[i].id, devices[i].username, devices[i].port, (unsigned)devices[i].timestamp_login, devices[i].online, devices[i].busy);
        
        for(j = 0; j < n_dev; j++) {
            fprintf(fptr, "%s %d %s ", devices[i].pm[j].usr, devices[i].pm[j].num, devices[i].pm[j].last_ts);
        }
        
        fprintf(fptr, "\n");

    }

    fclose(fptr);

    exit(0);

}

// *************************************************
// *                COMANDI DEVICE                 *
// *************************************************

void devSignup(int sd) {
    
    char usr[1024];
    char pwd[1024];
    char path[1024];
    int i;
    int id;

    FILE *fptr;
    
    // Ciclo per verificare username inserito dall'utente ed eventualmente registrarlo
    while(1) {

        // Ricevo username
        recv(sd, usr, sizeof(usr), 0);

        // Controllo gli username degli utenti registrati
        for(i = 0; i < n_dev; i++) {
            
            if(!strcmp(devices[i].username, usr))
                break;

        }

        // Se esco dal ciclo for e i < n_dev vuol dire che ho trovato un utente con lo username che sto verificando 
        if(i < n_dev) {
            // Avverto il device
            sendCommand(sd, NOT_OK);
            continue;
        }
        
        // Registro il nuovo utente
        devices[n_dev].id = n_dev; 
        strcpy(devices[n_dev].username, usr);
        devices[n_dev].online = false;
        devices[n_dev].busy = false;
        devices[n_dev].timestamp_login = 0;
        for(i = 0; i <= n_dev; i++) {
            strcpy(devices[n_dev].pm[i].usr, devices[i].username);
            devices[n_dev].pm[i].num = 0;
            strcpy(devices[n_dev].pm[i].last_ts, "[]");
        }
        for(i = 0; i <= n_dev; i++) {
            devices[n_dev].read_notify[i] = false;
        }
        // Aggiorno la struttura dei messaggi pendenti di ogni utente già registrato
        for(i = 0; i < n_dev; i++){          
            strcpy(devices[i].pm[n_dev].usr, usr);
            devices[i].pm[n_dev].num = 0;
            strcpy(devices[i].pm[n_dev].last_ts, "[]");
        } 
        // Aggiorno il vettore delle notifiche di ogni utente già registrato
        for(i = 0; i < n_dev; i++) {
            devices[i].read_notify[n_dev] = false;
        }

        id = n_dev;
        n_dev++;

        // Mando l'OK al device ed esco dal ciclo
        sendCommand(sd, OK);
        break;

    }

    // Ricevo password
    recv(sd, pwd, sizeof(pwd), 0);

     // Salvo la password nel file usr_pwd.txt (il file contiene record del tipo "usernamepassword")
    fptr = fopen("server/usr_pwd.txt", "a");
    fprintf(fptr, "%s\n", pwd);

    if(fptr)
        fclose(fptr);

    // mando l'OK al Device
    sendCommand(sd, OK);

    // Creo la cartella per salvare i messag pendenti inviati al nuovo utente
    strcpy(path, "server/pending_messages/");
    strcat(path, usr);
    mkdir(path, 0700);

    devOnlineSetup(sd, id);

}

void devIn(int sd) {
    
    char usr[1024];
    char pwd[1024];
    int i;

    FILE *fptr;

    // Ciclo per verificare che lo username inserito dall'utente sia presente tra quelli registrati
    while(1) {

        // Ricevo username
        recv(sd, usr, sizeof(usr), 0);

        // Controllo gli username degli utenti registrati
        for(i = 0; i < n_dev; i++) {
            
            if(!strcmp(devices[i].username, usr))
                break;

        }

        // Se esco dal ciclo for e i == n_dev vuol dire che non ho trovato nessun utente con lo username che sto verificando 
        if(i == n_dev) {
            // Avverto il device
            sendCommand(sd, NOT_OK);
            continue;
        }

        // Mando l'OK al device ed esco dal ciclo
        sendCommand(sd, OK);
        break;

    }

    while(1) {

        // Ricevo password (nel formato "usernamepassword")
        recv(sd, pwd, sizeof(pwd), 0);

        // Controllo che la stringa ricevuta sia presente nel file usr_pwd.txt
        fptr = fopen("server/usr_pwd.txt", "r");
        // Se non è presente avverto il device e attendo una nuova stringa
        if(!check_stringa(fptr, pwd)) {
            sendCommand(sd, NOT_OK);
            if(fptr)
                fclose(fptr);
            continue;
        }

        if(fptr)
            fclose(fptr);

        // mando l'OK al Server
        sendCommand(sd, OK);

        break;  // Stringa presente, quindi esco dal ciclo

    }

    devOnlineSetup(sd, i);

}

void devHanging(int sd) {

    int i, usr_id;
    char n[1024];
    char buffer[1024];

    // Ricevo l'id dall'utente
    recv(sd, buffer, sizeof(buffer), 0);

    usr_id = atoi(buffer);
    
    // Controllo per ogni altro utente se ho messaggi pendenti
    for(i = 0; i < n_dev; i++) {
        if(devices[usr_id].pm[i].num) {         // L'utente i ha lasciato messaggi pendenti
            // Preparo la stringa da inviare al device
            strcpy(buffer, devices[usr_id].pm[i].usr);
            strcat(buffer, " ");
            sprintf(n, "%d", devices[usr_id].pm[i].num);
            strcat(buffer, n);
            strcat(buffer, " ");
            strcat(buffer, devices[usr_id].pm[i].last_ts);
            // Invio la stringa "username num_pending last_timestamp" al device
            send(sd, buffer, strlen(buffer)+1, 0);
            while(1) {
                recv(sd, buffer, sizeof(buffer), 0);
                    if(strcmp(buffer, READY)){
                        continue;
                    }
                break;
            }
        }
    }

    sendCommand(sd, OK);
    
}

void devShow(int sd) {

    int i, usr_id;
    char buffer[1024];
    char usr[1024];
    char path[1024];
    char path1[1024];
    char *ret;
    FILE *fptr, *fptr1;

    // Ricevo l'id dall'utente
    recv(sd, buffer, sizeof(buffer), 0);
    usr_id = atoi(buffer);

    sendCommand(sd, READY);

    // Ricevo lo username dell' utente di cui si vogliono leggere i messaggi
    recv(sd, usr, sizeof(usr), 0);

    // Verifico che lo username ricevuto sia esistente e che abbia inviato messaggi
    for(i = 0; i < n_dev; i++) {
        if(!strcmp(devices[usr_id].pm[i].usr, usr) && devices[usr_id].pm[i].num != 0) {
            break;
        }
    }

    if(i < n_dev) {     // i è l'id dell'utente del quale il device vuole leggere i messaggi
        sendCommand(sd, OK);
    }
    else {
        sendCommand(sd, NOT_OK);
        return;
    }
    

    strcpy(path, "server/pending_messages/");
    strcat(path, devices[usr_id].username);
    strcat(path, "/");
    strcat(path, devices[usr_id].pm[i].usr);
    strcat(path, ".txt");

    strcpy(path1, devices[usr_id].pm[i].usr);
    strcat(path1, "/");
    strcat(path1, devices[usr_id].username);
    strcat(path1, ".txt");

    fptr = fopen(path, "r");
    fptr1 = fopen(path1, "a");

    while(1) {
        
        while(1) {
            recv(sd, buffer, sizeof(buffer), 0);
                if(strcmp(buffer, READY)){
                    continue;
                }
            break;
        }

        ret = fgets(buffer, 1024, fptr);

        if(!ret) {
            sendCommand(sd, OK);
            break;
        }

        send(sd, buffer, strlen(buffer)+1, 0);

        fprintf(fptr1, "*%s", buffer);
    }

    fclose(fptr);
    fclose(fptr1);
    remove(path);
    devices[usr_id].pm[i].num = 0;
    strcpy(devices[usr_id].pm[i].last_ts, "[]");

    if(devices[i].online) {
        notify(usr_id, i);
    }
    else {
        devices[i].read_notify[usr_id] = true;
    }    

}

void devOut(int sd) {
    
    char buffer[1024];
    char timestamp_out[1024];
    char timestamp_in[1024];
    char usr[1024];
    int port;
    char file_vecchio[1024];
    char file_nuovo[1024];
    int usr_id;
    time_t rawtime;
    FILE *fptr, *fptr1;

    // Mi sincronizzo con il device
    sendCommand(sd, READY);

    // Ricevo l'id dal device
    recv(sd, buffer, sizeof(buffer), 0);
    usr_id = atoi(buffer);

    // Sistemo la struttura dati
    devices[usr_id].online = false;
    devices[usr_id].timestamp_login  = 0;
    devices[usr_id].port = -1;

    // Aggiorno il registro inserendo il timestamp di logout dell'uente
    time(&rawtime);

    strcpy(file_vecchio, "server/registro.txt");
    strcpy(file_nuovo, "server/registro1.txt");

    fptr = fopen(file_vecchio, "r");
    fptr1 = fopen(file_nuovo, "a");

    
    while(fscanf(fptr, "%s", usr) == 1) {
        
        if(!strcmp(usr, devices[usr_id].username)) {
            fscanf(fptr, "%d %s %s", &port, timestamp_in, timestamp_out);
            timestampTranslate(rawtime, timestamp_out);
            fprintf(fptr1, "%s %d %s %s\n", usr, port, timestamp_in, timestamp_out);
            continue;
        }

        fscanf(fptr, "%d %s %s", &port, timestamp_in, timestamp_out);
        fprintf(fptr1, "%s %d %s %s\n", usr, port, timestamp_in, timestamp_out);

    }

    fclose(fptr);
    fclose(fptr1);

    remove(file_vecchio);
    rename(file_nuovo, "server/registro.txt");
    
}

void devChat(int sd) {
    
    char usr_dest[1024];
    char buffer[1024];
    int usr_id;
    int i;
    char path[1024];
    char last_ts[1024];
    char *pointer;
    FILE *fptr;

    sendCommand(sd, READY);

    // Ricevi l'id dell'utente
    recv(sd, buffer, sizeof(buffer), 0);
    usr_id = atoi(buffer);

    sendCommand(sd, OK);
    
    while(1) {

        // Ricevo lo username dell'utente con il quale si vuole iniziare una chat
        recv(sd, usr_dest, sizeof(usr_dest), 0);
        
        // Se ricevo "\q" vuol dire che l'utente non vuole chattare con nessuno quindi esco
        if(!strcmp(usr_dest, QUIT)) {
            return;
        }

        // Controllo che lo username ricevuto sia registrato all'app
        for(i = 0; i < n_dev; i++) {
            if(!strcmp(devices[i].username, usr_dest)) {
                break;
            }
        }
        
        if(i < n_dev) {
            if(devices[usr_id].pm[i].num > 0) {
                sendCommand(sd, PENDING);
                return;
            }
            sendCommand(sd, OK);
            break;
        }

        sendCommand(sd, NOT_OK);

    }

    while(1) {
        recv(sd, buffer, sizeof(buffer), 0);
        if(strcmp(buffer, READY)){
            continue;
        }
        break;
    }

    // Controllo che l'utente sia online e non impegnato in un'altra chat
    if(devices[i].online && !devices[i].busy) {     // utente online e libero

        sendCommand(sd, OK);

        while(1) {
            recv(sd, buffer, sizeof(buffer), 0);
                if(strcmp(buffer, READY)){
                    continue;
                }
                break;
        }

        // Mando al device il numero di porta dell'utente con il quale si vuole iniziare la chat
        sprintf(buffer, "%d", devices[i].port);
        send(sd, buffer, strlen(buffer)+1, 0);

        // Attendo l'OK di inizio chat dal device
        recv(sd, buffer, sizeof(buffer), 0);
        if(!strcmp(buffer, OK)) {
            devices[usr_id].busy = true;
            devices[i].busy = true;
        }
        
        return;

    }

    // Utente offline o occupato
    sendCommand(sd, NOT_OK);


    // ****** CHAT OFFLINE ******

    devices[usr_id].busy = true;

    // Ricevo i messaggi dal device e li bufferizzo (li salvo in un file come messaggi pendenti)
    strcpy(path, "server/pending_messages/");
    strcat(path, usr_dest);
    strcat(path, "/");
    strcat(path, devices[usr_id].username);
    strcat(path, ".txt");

    while(1) {

        recv(sd, buffer, sizeof(buffer), 0);

        if(!strcmp(buffer, QUIT)) {
            break;
        }

        // Salvo il messaggio nel file
        fptr = fopen(path, "a");
        fprintf(fptr, "%s", buffer);
        fclose(fptr);

        // Aggiorno la struttura dati dei messaggi pendenti dell'utente destinatario
        devices[i].pm[usr_id].num++;
        // Recupero il timestamp del messaggio
        pointer = buffer;
        sscanf(pointer, "%s", last_ts);      // La prima stringa è *
        pointer += strlen(last_ts)+1;
        sscanf(pointer, "%s", last_ts);      // La seconda stringa è [username] 
        pointer += strlen(last_ts)+1;
        sscanf(pointer, "%s", last_ts);      // La terza stringa è [timestamp]
        strcpy(devices[i].pm[usr_id].last_ts, last_ts);

        // Invio l'OK di avvenuta ricezione del messaggio al device
        sendCommand(sd, OK);

    }

    devices[usr_id].busy = false;

}

void devChatUsersOnline(int sd) { 

    char buffer[1024];
    int i;

    for(i = 0; i < n_dev; i++) {
        if(devices[i].online && !devices[i].busy) {
            strcpy(buffer, devices[i].username);
            send(sd, buffer, strlen(buffer)+1, 0);
            while(1) {
                recv(sd, buffer, sizeof(buffer), 0);
                if(strcmp(buffer, READY)){
                    continue;
                }
                break;
            }
        }
    }
    sendCommand(sd, OK);

}

void devChatUserAdd(int sd) {

    char buffer[1024];
    int i;

    sendCommand(sd, READY);

    // Ricevo lo username dell'utente da aggiungere
    recv(sd, buffer, sizeof(buffer), 0);

    for(i = 0; i < n_dev; i++) {
        if(!strcmp(devices[i].username, buffer)) {
            break;
        }
    }
    // Invio il numero di porta dell'utente
    sprintf(buffer, "%d", devices[i].port);
    send(sd, buffer, strlen(buffer)+1, 0);

    // Attendo l'OK dal device
    recv(sd, buffer, sizeof(buffer), 0);
    if(!strcmp(buffer, OK)) {
        devices[i].busy = true;
    }

}

void *tr_code(void *arg) {

    int dev_sd;
    char dev_cmd[1024];
    int ret;
    int i;

    dev_sd = *(int *)arg;

    while(1) {
                            
        // Ricevo un comando
        ret = recv(dev_sd, dev_cmd, sizeof(dev_cmd), 0);

        if(ret > 0) { 
            if(!strcmp(dev_cmd, CMD_SIGNUP)) {
                // Gestione comando signup
                devSignup(dev_sd);
            }
            else if(!strcmp(dev_cmd, CMD_IN)) {  
                // Gestione comando in
                devIn(dev_sd);
            }
            else if(!strcmp(dev_cmd, CMD_HANGING)) {
                // Gestione comando hanging
                devHanging(dev_sd);
            }
            else if(!strcmp(dev_cmd, CMD_SHOW)) {
                // Gestione comando show
                devShow(dev_sd);
            }
            else if(!strcmp(dev_cmd, CMD_OUT)) {
                // Gestione comando out
                devOut(dev_sd);
                close(dev_sd);
                free(arg);
                pthread_exit(NULL);
            }
            else if(!strcmp(dev_cmd, CMD_CHAT)) {
                // Gestione comando chat
                devChat(dev_sd);
            }
            else if(!strcmp(dev_cmd, CHAT_EXIT)) {
                // Gestione uscita del device dalla chat
                for(i = 0; i < n_dev; i++) {
                    if(devices[i].sd == dev_sd) {
                        devices[i].busy = false;
                        break;
                    }
                }
            }
            else if(!strcmp(dev_cmd, USERS_ONLINE)) {
                // Gestione richiesta lista utenti online
                devChatUsersOnline(dev_sd);
            }
            else if(!strcmp(dev_cmd, USER_ADD)) {
                // Gestione aggiunta utente alla chat di gruppo
                devChatUserAdd(dev_sd);
            }
            else if(!strcmp(dev_cmd, UPDATE_SD)) {
                // Aggiornamento dell'sd dell'utente in memoria
                update_dev_sd(dev_sd);
            }
        }     
        else {      // Il socket è stato chiuso
            close(dev_sd);
            free(arg);
            pthread_exit(NULL);
        }

    }

}

// Handler per la gestione di segnali di uscita improvvisa CTRL+C, CTRL+Z
void handler(int sig) {

    esc();

}


int main(int argc, char *argv[]) {
    
    int ret;
    struct sockaddr_in this_srv_addr;
    int this_srv_port;
    struct sockaddr_in cl_addr;
    int listener;
    int new_sd;
    char srv_cmd[1024];
    int addrlen;
    int i;
    pthread_t tr;
    int *arg;

    // Se non viene inserita nessuna porta dall'utente assegno di default la 4242
    if (argc == 1) 
        this_srv_port = 4242;

    else if (argc == 2) 
        this_srv_port = atoi(argv[1]);
    
    else 
        printf("SYNTAX ERROR\n");

    // Recupero le informazioni dal file "server_recover.txt" (se esiste)
    serverRecover();

    printf("\n************************** SERVER AVVIATO **************************\n\n");

    printSrvCommands();

    fdtInit();

    // Configurazione del socket di ascolto
    listener = socketCreate(&this_srv_addr, this_srv_port);

    listen(listener, 10);

    // Aggiungo il listener al master
    FD_SET(listener, &master);

    // Non ci sono altri socket nel set per cui il listener sara' il maggiore
    fdmax = listener;

    // Creazione cartella "server" (se non esiste)
    if(stat("server", &st) == -1)
        mkdir("server", 0700);
    
    // Creazione cartella "pending_messages" (se non esiste), per i messaggi pendenti
    if(stat("server/pending_messages", &st) == -1)
        mkdir("server/pending_messages", 0700);

    signal(SIGINT, handler);
    signal(SIGTSTP, handler);

    while(1) {

        read_fds = master;

        select(fdmax + 1, &read_fds, NULL, NULL, NULL);

        // Controllo se ci sono socket pronti
        for(i=0; i<=fdmax; i++) {
            
            if(FD_ISSET(i, &read_fds)) {        // trovato uno pronto

                if(i == 0) {        // STDIN pronto
                    
                    scanf("%s", srv_cmd);
                    if(!strcmp(srv_cmd, "help")) {
                        // gestione comando "help"
                        help();
                        sleep(1);
                        printSrvCommands();
                    }
                    else if(!strcmp(srv_cmd, "list")) {
                        // gestione comando "list"
                        list();
                        sleep(1);
                        printSrvCommands();
                    }
                    else if(!strcmp(srv_cmd, "esc")) {
                        // gestione comando "esc"
                        esc();
                    }
                    else{
                        printf("\nATTENZIONE: Comando non riconosciuto!\n\n");
                        sleep(1);
                        printSrvCommands();
                    }

                }
                else if(i == listener) {        // listener pronto

                    addrlen = sizeof(cl_addr);

                    new_sd = accept(listener, (struct sockaddr *)&cl_addr, (socklen_t *)&addrlen);

                    arg = (int *)malloc(sizeof(int));
                    *arg = new_sd;

                    ret = pthread_create(&tr, NULL, tr_code, arg);

                    if(ret) {
                        printf("ERRORE: Qualcosa e' andato storto durante la creazione del thread!");
                        exit(-1);
                    }

                }

            }

        }
    }

    return 0; 
}