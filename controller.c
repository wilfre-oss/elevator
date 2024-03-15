//
// Created by William on 2024-03-11.
//

#include <stdio.h>
#include <winsock2.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef WIN32
/* sleep in Unix sleeps in seconds, Sleep in window sleeps in millisecond */
#define sleep(X) Sleep((X)*1000)
#endif
#include "hardwareAPI.h"
int num_cabins;
pthread_mutex_t mutex;
pthread_cond_t eid[5] = {PTHREAD_COND_INITIALIZER,
                          PTHREAD_COND_INITIALIZER,
                          PTHREAD_COND_INITIALIZER,
                          PTHREAD_COND_INITIALIZER,
                          PTHREAD_COND_INITIALIZER};



void *controller(void *);
void *elevator(void *);

boolean stop = false;

int main(int argc, char **argv){
    pthread_t ctlThr;
    char *hostname;
    int port;
    pthread_t evThr[5];
    //
    if (argc != 3) {
        fprintf(stderr, "Usage: %s host-name port\n", argv[0]);
        fflush(stderr);
        exit(-1);
    }
    hostname = argv[1];
    if ((port = atoi(argv[2])) <= 0) {
        fprintf(stderr, "Bad port number: %s\n", argv[2]);
        fflush(stderr);
        exit(-1);
    }

    //
    initHW(hostname, port);



    if (pthread_mutex_init(&mutex, NULL) < 0) {
        perror("pthread_mutex_init");
        exit(1);
    }
    if (pthread_create(&ctlThr, NULL, controller, (void *) 0) != 0) {
        perror("pthread_create");
        exit(-1);
    }

    fprintf(stdout, "waiting for 3 seconds .. \n");
    fflush(stdout);

    sleep(3);
    fprintf(stdout, "checking number of cabins .. \n");
    fflush(stdout);
    sleep(3);

    fprintf(stdout, "starting threads ...\n");
    fflush(stdout);

    fprintf(stdout, "number of cabins: %d\n", num_cabins);
    fflush(stdout);
    for(int i = 0; i < num_cabins; i++){
        int *id = malloc(sizeof(int));
        *id = i;
        if (pthread_create(&evThr[i], NULL, elevator, id) != 0) {
            perror("pthread_create");
            exit(-1);
        }
    }
    (void) pthread_join(ctlThr, NULL);
    stop = true;

    for(int i = 0; i < num_cabins; i++){
        pthread_cond_signal(&eid[i]);
        (void) pthread_join(evThr[i], NULL);
    }
    terminate();
    return 0;
}


typedef struct {
    double position;
    int direction;
    int cabinButton;
    int destination[10];
    pthread_mutex_t dest;
}Cabin;

Cabin cabin[5];
void add_cabin_dest(int id, int floor);
void remove_cabin_dest(int id);
int select_cabin(int floor, int direction);
double speed = 0.157;

void *controller(void *argv){
    EventType e;
    EventDesc ed;


    whereIs(0);
    while(1){
        e = waitForEvent(&ed);

        switch (e) {
            case FloorButton:
                pthread_mutex_lock(&mutex);
                getSpeed();

                fprintf(stdout, "floor button: floor %d, type %d\n",
                    ed.fbp.floor, (int) ed.fbp.type);
                fflush(stdout);
                int id = select_cabin(ed.fbp.floor,(int) ed.fbp.type);
                add_cabin_dest(id, ed.fbp.floor);
                pthread_cond_signal(&eid[id]);
                pthread_mutex_unlock(&mutex);
                break;

            case CabinButton:
                pthread_mutex_lock(&mutex);
                fprintf(stdout, "cabin button: cabin %d, floor %d\n",
                        ed.cbp.cabin, ed.cbp.floor);
                fflush(stdout);
                cabin[ed.cbp.cabin - 1].cabinButton = ed.cbp.floor;
                pthread_cond_signal(&eid[ed.cbp.cabin - 1]);
                pthread_mutex_unlock(&mutex);
                break;

            case Position:
                if(ed.cp.cabin > num_cabins){num_cabins = ed.cp.cabin;}
                fprintf(stdout, "cabin position: cabin %d, position %f\n",
                        ed.cp.cabin, ed.cp.position);
                fflush(stdout);
                cabin[ed.cp.cabin - 1].position = ed.cp.position;
                pthread_cond_signal(&eid[ed.cbp.cabin - 1]);

                break;

            case Speed:
                pthread_mutex_lock(&mutex);
                fprintf(stdout, "speed: %f\n", ed.s.speed);
                fflush(stdout);
                speed = ed.s.speed * 1000;
                pthread_mutex_unlock(&mutex);
                break;

            case Error:
                pthread_mutex_lock(&mutex);
                fprintf(stdout, "error: \"%s\"\n", ed.e.str);
                fflush(stdout);
                pthread_mutex_unlock(&mutex);
                pthread_exit(NULL);
            }
    }
}

void *elevator(void *argv){
    int id = *(int*)argv;
    free(argv);
    cabin[id].cabinButton = -1;
    cabin[id].position = 0;
    cabin[id].direction = 0;
    for(int i = 0; i < 10; i++){
        cabin[id].destination[i] = -1;
    }

    int nextStop = -1;
    struct timespec timeout;
    cabin[id].dest = PTHREAD_MUTEX_INITIALIZER;


    while(!stop){
        pthread_mutex_lock(&mutex);
        if(cabin[id].destination[0] == -1 && cabin[id].cabinButton == -1){
            pthread_cond_wait(&eid[id], &mutex);
            nextStop = cabin[id].destination[0];
            whereIs(id + 1);
        }

        if(cabin[id].cabinButton != -1){

            add_cabin_dest(id, cabin[id].cabinButton);
            cabin[id].cabinButton = -1;
            whereIs(id + 1);
        }
        else if(nextStop == -1){
            pthread_cond_wait(&eid[id], &mutex);
            pthread_mutex_unlock(&mutex);
            continue;
        }

        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 0; // Wait for 0 seconds
        timeout.tv_nsec += 500000; // Wait for 500,000 nanoseconds (0.5 seconds)

        // Normalize the timeout
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec += 1;
            timeout.tv_nsec -= 1000000000;
        }
        if (pthread_cond_timedwait(&eid[id], &mutex, &timeout) != 0){
            whereIs(id + 1);
        }

        nextStop = cabin[id].destination[0];
        if(nextStop > cabin[id].position && cabin[id].direction < 1){
            handleMotor(id + 1, MotorUp);
            cabin[id].direction = 1;
            pthread_mutex_unlock(&mutex);
            continue;
        }
        else if(nextStop < cabin[id].position && cabin[id].direction > -1){
            handleMotor(id + 1, MotorDown);
            cabin[id].direction = -1;
            pthread_mutex_unlock(&mutex);
            continue;
        }
        else if((double)nextStop - cabin[id].position <= 0.04 &&
            (double)nextStop - cabin[id].position >= 0.0){
            fprintf(stdout, "stopping cabin\n");
            fflush(stdout);
            handleMotor(id + 1, MotorStop);
            handleDoor(id + 1, DoorOpen);
            pthread_mutex_unlock(&mutex);
            cabin[id].direction = 0;

            for(int i = 1; i < 100; i++){
                if(cabin[id].cabinButton != -1){
                    add_cabin_dest(id, cabin[id].cabinButton);
                    cabin[id].cabinButton = -1;
                }
                sleep((3.0/100.0));
            }
            pthread_mutex_lock(&mutex);
            handleDoor(id + 1, DoorClose);
            pthread_mutex_unlock(&mutex);
            remove_cabin_dest(id);
            for(int i = 1; i < 100; i++){
                if(cabin[id].cabinButton != -1){
                    add_cabin_dest(id, cabin[id].cabinButton);
                    cabin[id].cabinButton = -1;
                }
                sleep((3.0/100.0));
            }

            nextStop = cabin[id].destination[0];
            continue;
        }
        pthread_mutex_unlock(&mutex);

    }
}

void add_cabin_dest(int id, int floor){
    int position;
    int dest = floor;
    int temp;
    int dir;
    pthread_mutex_lock(&cabin[id].dest);

    position = (int)cabin[id].position;

    for(int i = 0; i < 10; i++){
        if(cabin[id].destination[i] == -1 || cabin[id].destination[i] == dest){
            cabin[id].destination[i] = dest;
            break;
        }

        if(position - cabin[id].destination[i] >= 0){
            dir = -1;
        } else {dir = 1;}

        if((dir == 1 && cabin[id].destination[i] > dest && dest > position) ||
            (dir == -1 && cabin[id].destination[i] < dest && dest < position)){
            temp = cabin[id].destination[i];
            cabin[id].destination[i] = dest;
            dest = temp;
        }
        position = cabin[id].destination[i];
    }
    pthread_mutex_unlock(&cabin[id].dest);
}

void remove_cabin_dest(int id){
    pthread_mutex_lock(&cabin[id].dest);

    for(int i = 1; i < 10; i++){
        cabin[id].destination[i - 1] = cabin[id].destination[i];
        if(cabin[id].destination[i] == -1 || i == 9){
            cabin[id].destination[i] = -1;
            break;
        }
    }

    pthread_mutex_unlock(&cabin[id].dest);
}

int select_cabin(int floor, int direction){
    double min_cost = INT_MAX;
    int best_cabin;
    for(int i = 0; i < num_cabins; i++){
        double cost = 0;
        int dir;


        cost += (double)abs((int)cabin[i].position - cabin[i].destination[0]) / speed + 6;
        for(int j = 1; j < 10; j++){
            if(cabin[i].destination[0] == -1){
                cost = (double)abs((int)cabin[i].position - floor) / speed;
                break;
            }
            else if(cabin[i].destination[j] == -1){
                if(cabin[i].destination[j - 1] == floor){
                    cost +=  6;
                }

                cost += (double)abs(cabin[i].destination[j - 1] - floor) / speed;

                break;
            }
            if(cabin[i].destination[j] > cabin[i].destination[j - 1]){
                dir = 1;
            } else { dir = -1;}

            if(dir == direction){
                if (cabin[i].destination[j] >= floor && dir == 1) {
                    cost += ((double)abs(cabin[i].destination[j - 1] - floor) / speed) + 6;
                    break;
                }
                else if (cabin[i].destination[j] <= floor && dir == 1) {
                    cost += ((double)abs(cabin[i].destination[j - 1] - floor) / speed) + 6;
                    break;
                }

            }
            cost += ((double)abs(cabin[i].destination[j] - cabin[i].destination[j - 1]) / speed) + 6;
        }

        if(cost < min_cost){
            min_cost = cost;
            best_cabin = i;
        }
    }
    return best_cabin;
}

void *init(void *argv){
    whereIs(0);
    EventDesc ed;
    for(int i = 0; i < 5; i ++){
        waitForEvent(&ed);
        if(ed.cp.cabin > num_cabins){
            num_cabins = ed.cp.cabin;
        }
    }
}