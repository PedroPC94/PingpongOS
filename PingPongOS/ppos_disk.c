#include "ppos_disk.h"
#include "disk.h"
#include "ppos.h"
#include "ppos-core-globals.h"

#include <signal.h>

struct sigaction action ;

static disk_t disk ;

task_t gerenciadorDeDisco;

void tratadorSinalDeDisco (int signum)
{
    //printf ("Recebi o sinal %d\n", signum) ;
    sem_down(&disk.semaforo);
    disk.sinal = 1;
    sem_up(&disk.semaforo);
    task_switch(&gerenciadorDeDisco);
}

void diskDriverBody (void * args)
{
    int operation;
    diskRequest_t * request;

    //printf ("criou a task:\n");
    while (1) 
    {
        //printf ("Driver de disco ativado\n");

        // obtém o semáforo de acesso ao disco
        sem_down(&disk.semaforo);
        //printf ("Driver de disco: Semaforo: %d \n", disk.semaforo.value);

        // se foi acordado devido a um sinal do disco
        if (disk.sinal == 1)
        {
            printf ("Driver de disco: Sinal Recebido\n");
            task_resume(disk.task);
            disk.status = DISK_STATUS_IDLE;
            disk.sinal = 0;
        //   acorda a tarefa cujo pedido foi atendido
        }
        // se o disco estiver livre e houver pedidos de E/S na fila
        if ((disk.status == DISK_STATUS_IDLE) && (disk.requestQueue != NULL))
        {
            printf ("Driver de disco: Executando operacao\n");
            // escolhe na fila o pedido a ser atendido, usando FCFS
            // solicita ao disco a operação de E/S, usando disk_cmd()
            request = (diskRequest_t*) queue_remove((queue_t **)disk.requestQueue, (queue_t *)disk.requestQueue);
            printf("Driver de disco: Removeu da Queue\n");
            // printf ("request Operation: %d \n", request->operation ) ;
            // printf ("request Block: %d \n",request->block ) ;

            operation = request->operation;

            if(operation == DISK_CMD_READ){
                int cmd = disk_cmd(DISK_CMD_READ, request->block, request->buffer);
                disk.status = DISK_STATUS_READ;
            } else if (operation == DISK_CMD_WRITE) {
                int cmd = disk_cmd(DISK_CMD_WRITE, request->block, request->buffer);
                disk.status = DISK_STATUS_WRITE;
            }
            disk.task = request->taskOriginal;

            printf ("Driver de disco: realizou operação\n") ;

            free(request);
            printf("Driver de disco: Liberou espaço do request\n");
        }
 
        // libera o semáforo de acesso ao disco
        sem_up(&disk.semaforo);
        //printf ("Driver de disco: Semaforo: %d \n", disk.semaforo.value);
    
        // suspende a tarefa corrente (retorna ao dispatcher)
        task_yield();
   }
}

int disk_mgr_init (int *numBlocks, int *blockSize){
    int start = disk_cmd(DISK_CMD_INIT, 0, 0);
    if( start == -1){
        return -1;
    }

    *numBlocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    *blockSize = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);

    //printf ("inicializando parametros do disco \n") ;

    disk.numBlocks = disk_cmd(DISK_CMD_DISKSIZE, 0, 0);
    disk.blockSize = disk_cmd(DISK_CMD_BLOCKSIZE, 0, 0);
    disk.status = disk_cmd(DISK_CMD_STATUS, 0, 0);
    disk.requestQueue = NULL;
    disk.sinal = 0;
    //disk.task = NULL;

    int sem = sem_create(&disk.semaforo, 1);

    //printf ("numBlocs: %d \n", disk.numBlocks ) ;
    //printf ("blockSize: %d \n", disk.blockSize ) ;
    //printf ("status: %d \n", disk.status ) ;
    //printf ("Semaforo status: %d \n\n", disk.semaforo.value ) ;

    //iniciando a task gerenciadora de disco
    
    task_create(&gerenciadorDeDisco, diskDriverBody, NULL);
    gerenciadorDeDisco.isUserTask = 0;

    //definindo a funcao de tratamento de sinal
    action.sa_handler = tratadorSinalDeDisco;
    sigemptyset (&action.sa_mask) ;
    action.sa_flags = 0 ;
    if (sigaction (SIGUSR1, &action, 0) < 0)
    {
        perror ("Erro em sigaction: ") ;
        exit (1) ;
    }

    return 0;
}

// leitura de um bloco, do disco para o buffer
int disk_block_read (int block, void *buffer){

    printf("Pedido de read recebido\n");

    diskRequest_t* request;

    // obtém o semáforo de acesso ao disco
    int sem = sem_down(&disk.semaforo);

    request = malloc(sizeof(diskRequest_t));
    request->operation = DISK_CMD_READ;
    request->block = block;
    request->buffer = buffer;
    request->taskOriginal = taskExec;
    request->prev = NULL;
    request->next = NULL;

    printf("Pedido de read recebido 2\n");

    // inclui o pedido na fila_disco
    queue_append((queue_t **)&disk.requestQueue, (queue_t *)request);

    printf("Pedido de read recebido 3\n");

    if(gerenciadorDeDisco.state == 's'){
        // acorda o gerente de disco (põe ele na fila de prontas)
        task_resume(&gerenciadorDeDisco);
    }

    printf("Pedido de read recebido 4\n");

    //printf("diskRequest size: [%d]\n", queue_size((queue_t*)disk.requestQueue));

    // libera semáforo de acesso ao disco
    sem_up(&disk.semaforo);

    // suspende a tarefa corrente (retorna ao dispatcher)
    //task_suspend(taskExec, &disk.task);
    task_yield();

    return 0;
}

// escrita de um bloco, do buffer para o disco
int disk_block_write (int block, void *buffer){

    printf("Pedido de write recebido\n");

    diskRequest_t* request;

    // obtém o semáforo de acesso ao disco
    int sem = sem_down(&disk.semaforo);

    request = malloc(sizeof(diskRequest_t));
    request->operation = DISK_CMD_WRITE;
    request->block = block;
    request->buffer = buffer;
    request->taskOriginal = taskExec;
    request->prev = NULL;
    request->next = NULL;

    // inclui o pedido na fila_disco
    queue_append((queue_t **)&disk.requestQueue, (queue_t *)request);

    if(gerenciadorDeDisco.state == 's'){
        // acorda o gerente de disco (põe ele na fila de prontas)
        task_resume(&gerenciadorDeDisco);
    }

    //printf("diskRequest size: [%d]\n", queue_size((queue_t*)disk.requestQueue));

    // libera semáforo de acesso ao disco
    sem_up(&disk.semaforo);

    // suspende a tarefa corrente (retorna ao dispatcher)
    task_yield();

    return 0;
}