#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <vector>
#include <sys/time.h>
#include "hw2_output.h"

// Use namespace std for simplicity
//  - note that it is not allowed to use stdout. Use it for the debugging
using namespace std;

//General variables
vector<vector<pthread_mutex_t>> lock_list; // binary mask for determining which area is locked and which are not

vector<vector<int>> locked; // locked to show that a grid is already locked

//Phase 1 variables
pthread_mutex_t locking_up; // only one soldier can lock in a mean time

vector<vector<int>> ciggbuts_grid; // show the number of ciggbuts

pthread_cond_t okToLock;             
int trying_to_lock = 0;
int finished_thread = 0;

// Phase 2 variables:
int total_privates;
int took_a_break = 0;
int BREAK_ORDER_RECIEVED = 0; 
int STOP_ORDER_RECIEVED = 0;
int CONTINUE_ORDER_RECIEVED = 0;

pthread_mutex_t finishing;
pthread_mutex_t taking_break;
pthread_mutex_t order_executing;
pthread_cond_t take_break; // threads will sleep on this varaible
pthread_cond_t waiting_break; // threads will sleep on this varaible
pthread_mutex_t took_break_safely;

pthread_mutex_t fakeMutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t area_checking = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fakeCond = PTHREAD_COND_INITIALIZER;

// Phase 3 variables


vector<vector<pthread_mutex_t>> ss_lock_list; // Another lock list but for 
                                              // sneaky smokers classes so that
                                              // sneaky smokers can use lock_list
                                              // for their locking/unlocking whereas
                                              // proper private will use but proper private
                                              // and the sneaky smoker list




class ProperPrivate{ // For Phase 1
    public:
    vector<int> area_size; // how much area does it control
    
    vector<int> current_area; // determine which area are we ate
    int current_index;

    vector<vector<int>> to_do; // list of index pairs 
    int gid; // unique number
    int tg; // speed of the picking ciggbuts

    int is_working; // does our private is waiting or working?

    int debug; // for printing

        ProperPrivate(vector<int> area_size_, vector<vector<int>> to_do_, int gid_, int tg_, int debug_){ // Define initializer for the proper private.
            
            // Initialize simple parameters
            area_size = area_size_;
            to_do = to_do_;
            gid = gid_;
            tg = tg_;

            current_area = to_do_[0];
            current_index = 0;
            is_working = 0; 
            
            // If debug set true, stdout/stderr commands should be allowed
            debug = debug_;
            if(debug){
                cout << "ProperPrivate constructor successfull. gid : " << gid << endl;
            }

        }

        void workAlert(){ // Set true after we start working
            is_working = 1;

            if(debug){
                cout << "Private with gid " << gid << " alerted. \n" << endl;
            }
        }

        int setNextArea(){ // Change the next area 
            if(current_index+1 < to_do.size()){
                current_index++;
                current_area = to_do[current_index];
                return 1;
            }
            
            return 0;
        }
        int checkAvaible(vector<int> &area){
            // Check the grid for the area (i,j). If any one of the cell is locked, return false.
            // Try locking the grids, if cant lock any of them, unlock all the locks
            int start_i = area[0];
            int start_j = area[1];
            int locking_failed = 0;

            vector<vector<int>> self_locked_areas_index;
            vector<vector<int>> ss_locked_areas_index;

            for(int i = 0; i < area_size[0]; i++){
                for(int j = 0; j < area_size[1]; j++){
            
                    int smoker_does_not_blocks = (pthread_mutex_trylock(&ss_lock_list[start_i + i][start_j + j]) == 0);
                    
                    if(smoker_does_not_blocks){ // area unoccupied, unlock it 
                        pthread_mutex_unlock(&ss_lock_list[start_i + i][start_j + j]);
                    }

                    if(!BREAK_ORDER_RECIEVED && !STOP_ORDER_RECIEVED && smoker_does_not_blocks && pthread_mutex_trylock(&lock_list[start_i + i][start_j + j]) == 0){
                        vector<int> locked_area = {start_i + i, start_j + j}; // we have locked this area
                        self_locked_areas_index.push_back(locked_area);
                        locked[start_i + i][start_j + j] = 1;
                    }
                    else{
                        if(debug && !STOP_ORDER_RECIEVED && !BREAK_ORDER_RECIEVED){
                            cout << " GID : " <<  gid << " failed at locking " << start_i + i << " | " 
                            <<  start_j + j<< endl;
                        }

                        locking_failed = 1;
                        break;
                        
                    }
                }
 
                if(locking_failed == 1){
                    break;
                }
            }

            if(BREAK_ORDER_RECIEVED || STOP_ORDER_RECIEVED){
                locking_failed = 1;
            }

            if(locking_failed){
                for(int i = 0; i < self_locked_areas_index.size(); i++){ // Unlocked the locked cells if failed
                    int i_index = self_locked_areas_index[i][0];
                    int j_index = self_locked_areas_index[i][1];
                    pthread_mutex_unlock(&lock_list[i_index][j_index]); // cant lock due to pp
                    if(debug){
                        cout << "[SS COULDNT LOCK] GID : " << gid << " unlocked index " << i_index << " | " << j_index << endl;
                    }
                }
            }

            if(!locking_failed){ // we have locked successfully
                workAlert();
            }

            return !locking_failed;
        }

    void beginWorking(vector<int> area){
        // Begin working with current_area. 
        // Remove ciggbuts from the grids. Since grid indexes are locked, no need to create
        // another mutex.
        int start_i = area[0];
        int start_j = area[1];
        
        int i,j;
        for(i = 0; i < area_size[0]; i++){  // for each row in area
            for(j = 0; j < area_size[1]; j++){ // for each col in area
                while(ciggbuts_grid[start_i +i][start_j + j] > 0){ // until there is no ciggbut, remove one
                    if(BREAK_ORDER_RECIEVED || STOP_ORDER_RECIEVED){ // break from the loop
                        break;
                    }


                struct timespec ts;
                struct timeval now;
                int rt = 0;

                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += tg*1000;

                pthread_mutex_lock(&fakeMutex);

                do {
                    rt = pthread_cond_timedwait(&fakeCond, &fakeMutex, &ts);
                    }
                while (rt == 0);

                pthread_mutex_unlock(&fakeMutex);

                    // TODO :  Change this to cond timed wait to get better result

                    if(BREAK_ORDER_RECIEVED || STOP_ORDER_RECIEVED){ // break from the loop
                        break;
                    }
                    ciggbuts_grid[start_i +i][start_j + j] -= 1;
                    hw2_notify(PROPER_PRIVATE_GATHERED, gid, start_i + i, start_j + j);


                }
            }
            
        }

        if(!BREAK_ORDER_RECIEVED && !STOP_ORDER_RECIEVED){
            hw2_notify(PROPER_PRIVATE_CLEARED, gid, 0, 0);
        }
        //Unlock areas after cleaning/taking a break
        //pthread_mutex_lock(&area_checking);
        for(int i = 0; i <  area_size[0]; i++){  // for each row in area
            for(int j = 0; j <  area_size[1]; j++){ // for each col in area
                locked[start_i + i][start_j + j] = 0;
                pthread_mutex_unlock(&lock_list[start_i + i][start_j + j]);
                if(debug){
                    cout << start_i + i << " | " << start_j + j  << " is unlocked by " << gid << endl;
                }
            }   
            
        }
        //pthread_mutex_unlock(&area_checking);
    }

};

class Commander{ // For Phase 2
    public:
    vector<int> orders_t;
    vector<string> orders_string;
    int order_number;

    Commander(vector<int> orders_t_, vector<string> orders_string_, int order_number_){
        orders_t = orders_t_;
        orders_string = orders_string_;
        order_number = order_number_;
    }

};

class SneakySmoker{ // For Phase 3
    public:
    vector<int> current_area; // determine which area are we ate
    vector<vector<vector<int>>> self_locked_boundaries;
    int current_index;
    int current_ciggs;

    vector<vector<int>> to_do; // list of index pairs 
    vector<int> no_ciggs;
    int sid; // unique number
    int tg; // speed of the picking ciggbuts

    int is_smoking; // does our private is waiting or working?

    int debug; // for printing

        SneakySmoker(vector<int> no_ciggs_, vector<vector<int>> to_do_, int sid_, int tg_, int debug_){ // Define initializer for the proper private.
            
            // Initialize simple parameters
            no_ciggs = no_ciggs_;
            to_do = to_do_;
            sid = sid_;
            tg = tg_;

            current_area = to_do[0];
            current_ciggs = no_ciggs[0];
            current_index = 0;
            is_smoking = 0; 
            
            // If debug set true, stdout/stderr commands should be allowed
            debug = debug_;
            if(debug){
                cout << "SneakySmoker constructor successfull. gid : " << sid << endl;
            }

        }

        void smokeAlert(){ // Set true after we start working
            is_smoking = 1;

            if(debug){
                cout << "Smoker with sid " << sid << " alerted. \n" << endl;
            }
        }

        int setNextArea(){ // Change the next area 
            if(current_index+1 < to_do.size()){
                current_index++;
                current_area = to_do[current_index];
                current_ciggs = no_ciggs[current_index];
                return 1;
            }
            
            return 0;
        }
        int checkAvaible(vector<int> &area){
            // Check the grid for the area (i,j). If any one of the cell is locked, return false.
            // Try locking the grids, if cant lock any of them, unlock all the locks
            int start_i = area[0];
            int start_j = area[1];
            int locking_failed = 0;

            vector<vector<int>> self_locked_areas_index;
            for(int i = -1; i < 2; i++){
                for(int j = -1; j < 2; j++){
            
                    if(pthread_mutex_trylock(&lock_list[start_i + i][start_j + j]) == 0){
                        vector<int> locked_area = {start_i + i, start_j + j}; // we have locked this area
                        self_locked_areas_index.push_back(locked_area);
                        locked[start_i + i][start_j + j] = 1;
                    }
                    else{
                        if(pthread_mutex_trylock(&ss_lock_list[start_i + i][start_j + j]) == 0){ // If we can lock the area (PP)
                           
                            if(debug){
                                cout << " SID : " <<  sid << " failed at locking " << start_i + i << " | " 
                                <<  start_j + j<< endl;
                            }

                            locking_failed = 1;
                            pthread_mutex_unlock(&ss_lock_list[start_i + i][start_j + j]); 
                            break;
                        }
                        else{ // Smoker encountered
                            if(i == 0 && j == 0){
                                locking_failed = 1;
                            }
                            break;
                        }


                        
                    } 
                }
                if(locking_failed == 1){
                    break;
                }
            }

            if(locking_failed){
                for(int i = 0; i < self_locked_areas_index.size(); i++){ // Unlocked the locked cells if failed
                    int i_index = self_locked_areas_index[i][0];
                    int j_index = self_locked_areas_index[i][1];
                    pthread_mutex_unlock(&lock_list[i_index][j_index]);
                    if(debug){
                        cout << "[COULDNT LOCK] SID : " << sid << " unlocked index " << i_index << " | " << j_index << endl;
                    }
                }
            }



            if(!locking_failed){ // we have locked successfully
                vector<vector<int>> self_locked_area;
                for(int i = 0; i < self_locked_areas_index.size(); i++){ // Unlocked the other cells 
                        int i_index = self_locked_areas_index[i][0];
                        int j_index = self_locked_areas_index[i][1];
                        
                        
                        if(pthread_mutex_trylock(&ss_lock_list[i_index][j_index]) == 0){ // Non-blocking lock, doesnt matter

                            if(debug){
                                cout << "[LOCKED AND UNLOCKING] SID : " << sid << " unlocked index " << i_index << " | " << j_index << endl;
                            }
                            self_locked_area.push_back(self_locked_areas_index[i]);                                                   
                        } 

                        if(i_index == start_i && j_index == start_j){  // Only the cell smokers at should be locked
                            continue;
                        }
                        pthread_mutex_unlock(&lock_list[i_index][j_index]);


                    }
                self_locked_boundaries.push_back(self_locked_area);
                smokeAlert();
            }

            return !locking_failed;
        }

    void beginSmoking(vector<int> area, int ciggs_amount){
        // Begin working with current_area. 
        // Remove ciggbuts from the grids. Since grid indexes are locked, no need to create
        // another mutex.
        int start_i = area[0];
        int start_j = area[1];
        
        int i = -1,j = -1;
        int finished_smoking = 0;

        while(!finished_smoking && !STOP_ORDER_RECIEVED){
            /*
            I am really not proud of this implementation. For those who are wondering why I did it like
            this, subscribe to my youtube channel PewDiePie for further clarification. Thank you.
            */

            for(j = -1; j < 1; j++){ // j = -1
                if(STOP_ORDER_RECIEVED){ // break from the loop
                            break;
                    }
                struct timespec ts;
                struct timeval now;
                int rt = 0;

                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += tg*1000;

                pthread_mutex_lock(&fakeMutex);

                do {
                    rt = pthread_cond_timedwait(&fakeCond, &fakeMutex, &ts);
                    }
                while (rt == 0);

                pthread_mutex_unlock(&fakeMutex);
                    
                    // TODO :  Change this to cond timed wait to get better result

                    if(STOP_ORDER_RECIEVED){ // break from the loop
                        break;
                    }
                    ciggbuts_grid[start_i +i][start_j + j] += 1;
                    ciggs_amount -= 1;
                    hw2_notify(SNEAKY_SMOKER_FLICKED, sid, start_i + i, start_j + j);

                    if(ciggs_amount == 0){
                        finished_smoking = 1;
                        break;
                    }
            }

            if(finished_smoking || STOP_ORDER_RECIEVED){
                break;
            }

            for(i = -1; i < 1; i++){ // j = -1
                if(STOP_ORDER_RECIEVED){ // break from the loop
                            break;
                    }
                    usleep(tg); // wait before removing after locking
                    // TODO :  Change this to cond timed wait to get better result

                    if(STOP_ORDER_RECIEVED){ // break from the loop
                        break;
                    }
                    ciggbuts_grid[start_i +i][start_j + j] += 1;
                    ciggs_amount -= 1;
                    hw2_notify(SNEAKY_SMOKER_FLICKED, sid, start_i + i, start_j + j);

                    if(ciggs_amount == 0){
                        finished_smoking = 1;
                        break;
                    }
            }

            if(finished_smoking || STOP_ORDER_RECIEVED){
                break;
            }

            for(j = 1; j > -1; j--){ // j = -1
                if(STOP_ORDER_RECIEVED){ // break from the loop
                            break;
                    }
                    usleep(tg); // wait before removing after locking
                    // TODO :  Change this to cond timed wait to get better result

                    if(STOP_ORDER_RECIEVED){ // break from the loop
                        break;
                    }
                    ciggbuts_grid[start_i +i][start_j + j] += 1;
                    ciggs_amount -= 1;
                    hw2_notify(SNEAKY_SMOKER_FLICKED, sid, start_i + i, start_j + j);

                    if(ciggs_amount == 0){
                        finished_smoking = 1;
                        break;
                    }
            }

            if(finished_smoking || STOP_ORDER_RECIEVED){
                break;
            }

            for(i = 1; i > -1; i--){ // j = -1
                if(STOP_ORDER_RECIEVED){ // break from the loop
                            break;
                    }
                    usleep(tg); // wait before removing after locking
                    // TODO :  Change this to cond timed wait to get better result

                    if(STOP_ORDER_RECIEVED){ // break from the loop
                        break;
                    }
                    ciggbuts_grid[start_i +i][start_j + j] += 1;
                    ciggs_amount -= 1;
                    hw2_notify(SNEAKY_SMOKER_FLICKED, sid, start_i + i, start_j + j);

                    if(ciggs_amount == 0){
                        finished_smoking = 1;
                        break;
                    }
            }

            if(finished_smoking || STOP_ORDER_RECIEVED){
                break;
            }
                
        }

        if(!STOP_ORDER_RECIEVED){
            hw2_notify(SNEAKY_SMOKER_LEFT, sid, 0, 0);
        }
        //Unlock the area after smoking/stopping 
        pthread_mutex_unlock(&lock_list[start_i][start_j]);

        //Unlock the boundaries after smoking/stopping
        vector<vector<int>> current_boundaries = self_locked_boundaries[current_index];
        for(int i = 0; i < current_boundaries.size();i++){ // only unlock the boundaries that you have locked
            int i_index = current_boundaries[i][0];
            int j_index = current_boundaries[i][1];
            pthread_mutex_unlock(&ss_lock_list[i_index][j_index]);
        }

    }

};


void *hw2_phase1_begin(ProperPrivate *pp){
    int continue_executing = 1; // get rid of recursive overhead
    while(!STOP_ORDER_RECIEVED && continue_executing) {
        vector<int> area = pp->current_area;

        pthread_mutex_lock(&taking_break); 
        if(BREAK_ORDER_RECIEVED){ // Take a break if order is recieved
            took_a_break += 1;

            //cout << "Took a break : " << took_a_break << " finished : " << finished_thread << endl;
            if(took_a_break + finished_thread == total_privates){
                pthread_cond_signal(&waiting_break);
            }
            pthread_cond_wait(&take_break,&taking_break);
            took_a_break -= 1;
            if(STOP_ORDER_RECIEVED){
                pthread_mutex_unlock(&taking_break);
                return NULL;
            }
            hw2_notify(PROPER_PRIVATE_CONTINUED,pp->gid,0,0);
        }
        pthread_mutex_unlock(&taking_break); 
        //cout << " GID : " << pp->gid << " sleeps." << endl;
        //cout << " GID : " << pp->gid << " woken up." << endl;

        // For specific PraivateProper object, begin locking area.
        int canLock = 0;
        pthread_mutex_lock(&locking_up); // only one thread can look for empty spot
        //cout << "Current area is : "  << area[0] << " | " << area[1] <<  " GID : " << pp->gid << endl;
        while(!canLock){ // not busy waiting, loop until thread finds the area


            if(BREAK_ORDER_RECIEVED || STOP_ORDER_RECIEVED){
                // Unlock area here
                for(int i = 0; i < pp->area_size[0]; i++){
                    for(int j = 0; j < pp->area_size[1]; j++){
                        pthread_mutex_unlock(&lock_list[area[0] + i][area[1] + j]);
                    }
                }
                break;
            }

            //pthread_mutex_lock(&area_checking);
            canLock = pp->checkAvaible(area);
            //pthread_mutex_unlock(&area_checking);

            if(BREAK_ORDER_RECIEVED || STOP_ORDER_RECIEVED){
                // Unlock area here
                for(int i = 0; i < pp->area_size[0]; i++){
                    for(int j = 0; j < pp->area_size[1]; j++){
                        pthread_mutex_unlock(&lock_list[area[0] + i][area[1] + j]);
                    }
                }
                break;
            }

            if(canLock == 0){
                //pthread_mutex_unlock(&locking_up); // since we cant lock, let other threads in
                trying_to_lock += 1;
                //cout << "[SLEEP] Trying to lock is now : " << trying_to_lock << endl;
                pthread_cond_wait(&okToLock,&locking_up);  // gets unlocked after a thread finishes an area
                trying_to_lock -= 1;
                // << "[WOKEN] Trying to lock is now : " << trying_to_lock << endl;
                //pthread_mutex_lock(&locking_up); // get the lock again, if another thread is trying to lock fails and waits
            }
        }
        pthread_mutex_unlock(&locking_up); // area is determined and we are ready to work
        if(STOP_ORDER_RECIEVED){
            return NULL;
        }
        else if(BREAK_ORDER_RECIEVED){
            hw2_notify(PROPER_PRIVATE_TOOK_BREAK,pp->gid,0,0);
            continue;
        }
        hw2_notify(PROPER_PRIVATE_ARRIVED, pp->gid, area[0], area[1]);

        

        pp->beginWorking(area); // begin working on the current area

    
        if(trying_to_lock != 0){ // if there is a thread that tries to lock but couldnt, unlock it
            pthread_cond_broadcast(&okToLock);
        }

        if(BREAK_ORDER_RECIEVED){
            hw2_notify(PROPER_PRIVATE_TOOK_BREAK,pp->gid,0,0);
            continue;
        }
        else if(STOP_ORDER_RECIEVED){
            return NULL;
        }

 
        if(!BREAK_ORDER_RECIEVED){
            if(pp->setNextArea()){// If ProperPrivate has still area to do            pthread_cond_wait(&take_break,&taking_break);
                continue_executing = 1;
            } 
            else{
                continue_executing = 0;
            }
        }

    }    
    return NULL;
}

void *hw2_phase1_begin_wrapper(void *pp_){
    ProperPrivate *pp = (ProperPrivate *) pp_; // void pointer to pp pointer
    hw2_notify(PROPER_PRIVATE_CREATED, pp->gid, 0, 0);  // alert the notifier

    hw2_phase1_begin(pp);

    pthread_mutex_lock(&finishing); // Lock since the varaible @finished_thread  has a race condition
    finished_thread += 1;
    pthread_mutex_unlock(&finishing);

    if(STOP_ORDER_RECIEVED){
        hw2_notify(PROPER_PRIVATE_STOPPED,pp->gid,0,0);
    }
    else{
        hw2_notify(PROPER_PRIVATE_EXITED,pp->gid,0,0);
    }
}

void *hw2_phase3_begin(SneakySmoker *ss){
    int continue_executing = 1; // get rid of recursive overhead
    while(!STOP_ORDER_RECIEVED && continue_executing) {
        vector<int> area = ss->current_area;
        int cigg_number = ss->current_ciggs;
        //cout << " GID : " << pp->gid << " sleeps." << endl;
        //cout << " GID : " << pp->gid << " woken up." << endl;

        // For specific PraivateProper object, begin locking area.
        int canLock = 0;
        pthread_mutex_lock(&locking_up); // only one thread can look for empty spot
        //cout << "Current area is : "  << area[0] << " | " << area[1] <<  " GID : " << pp->gid << endl;
        while(!canLock){ // not busy waiting, loop until thread finds the area

            if(STOP_ORDER_RECIEVED){
                break;
            }


            canLock = ss->checkAvaible(area);

            if(STOP_ORDER_RECIEVED){
                break;
            }

            if(canLock == 0){
                //pthread_mutex_unlock(&locking_up); // since we cant lock, let other threads in
                trying_to_lock += 1;
                //cout << "[SLEEP] Trying to lock is now : " << trying_to_lock << endl;
                pthread_cond_wait(&okToLock,&locking_up);  // gets unlocked after a thread finishes an area
                trying_to_lock -= 1;
                //cout << "[WOKEN] Trying to lock is now : " << trying_to_lock << endl;
                //pthread_mutex_lock(&locking_up); // get the lock again, if another thread is trying to lock fails and waits
            }
        }
        pthread_mutex_unlock(&locking_up); // area is determined and we are ready to work
        if(STOP_ORDER_RECIEVED){
            return NULL;
        }

        hw2_notify(SNEAKY_SMOKER_ARRIVED, ss->sid, area[0], area[1]);


        ss->beginSmoking(area,cigg_number); // begin working on the current area


        if(trying_to_lock != 0){ // if there is a thread that tries to lock but couldnt, unlock it
            pthread_cond_broadcast(&okToLock);
        }

        if(STOP_ORDER_RECIEVED){
            return NULL;
        }

 
        if(ss->setNextArea()){// If ProperPrivate has still area to do            pthread_cond_wait(&take_break,&taking_break);
            continue_executing = 1;
        } 
        else{
            continue_executing = 0;
        }
        

    }    
    return NULL;
}

void *hw2_phase3_begin_wrapper(void *ss_){
    SneakySmoker *ss = (SneakySmoker *) ss_; // void pointer to pp pointer
    hw2_notify(SNEAKY_SMOKER_CREATED,ss->sid,0,0);
    hw2_phase3_begin(ss);

    if(STOP_ORDER_RECIEVED){
        hw2_notify(SNEAKY_SMOKER_STOPPED,ss->sid,0,0);
    }
    else{
        hw2_notify(SNEAKY_SMOKER_EXITED,ss->sid,0,0);
    }
}

void *hw2_phase2_begin_wrapper(void *arg_){
    Commander* arg = (Commander *) arg_;

    int order_number = arg->order_number;
    vector<int> orders_t= arg->orders_t;
    vector<string> orders_string= arg->orders_string;

    int previous_t = 0;

    for(int i = 0; i < order_number; i++){ // Execute the orders
        int order_t = orders_t[i];
        string order = orders_string[i];

        usleep(order_t - previous_t); // sleep for order_t - previous_t msec
        pthread_mutex_lock(&took_break_safely); // First lock is success
        if(order == "break"){

            hw2_notify(ORDER_BREAK,0,0,0);
            BREAK_ORDER_RECIEVED = 1;
            CONTINUE_ORDER_RECIEVED = 0;
            pthread_cond_broadcast(&okToLock);
            pthread_cond_broadcast(&fakeCond);

            if(took_a_break + finished_thread!= total_privates){
                pthread_cond_wait(&waiting_break,&took_break_safely); // wait after break for next orders
            }

        }
        else if(order == "continue"){
            hw2_notify(ORDER_CONTINUE,0,0,0);
            BREAK_ORDER_RECIEVED = 0;
            CONTINUE_ORDER_RECIEVED = 1;
            pthread_cond_broadcast(&take_break);
        }
        else if(order == "stop"){
            hw2_notify(ORDER_STOP,0,0,0);
            BREAK_ORDER_RECIEVED = 0;
            CONTINUE_ORDER_RECIEVED = 0;
            STOP_ORDER_RECIEVED = 1;

            pthread_cond_broadcast(&take_break);
            pthread_cond_broadcast(&okToLock);
            pthread_cond_broadcast(&fakeCond);
        }
        else{
            perror("An error occured in the input.");
        }
        pthread_mutex_unlock(&took_break_safely); // First lock is success
        previous_t = order_t;
    }
    return NULL;
}
int main(int argc, char *argv[]){

    hw2_init_notifier();
    int PHASE_2 = 1;
    int PHASE_3 = 1;

    int debug_s = 0;
    int debug_p = 0;

    // Input the arguments and save them in the correct objects
    int Gi, Gj;
    cin >> Gi >> Gj; // Grid size


    pthread_mutex_init(&locking_up,NULL); // Initialize the locks
    pthread_mutex_init(&taking_break,NULL); // Initialize the locks
    pthread_mutex_init(&finishing,NULL); // Initialize the locks
    pthread_mutex_init(&took_break_safely,NULL); // Initialize the locks

    pthread_cond_init(&okToLock, NULL);
    pthread_cond_init(&take_break, NULL);
    pthread_cond_init(&waiting_break, NULL);

    int num;
    for(int i = 0; i < Gi; i++){ // Grid initial ciggbutts
        vector<int> grid_row(Gj);
        vector<int> lock_row(Gj);
        vector<pthread_mutex_t> mutex_row(Gj);
        vector<pthread_mutex_t> ss_mutex_row(Gj);

        for(int j = 0; j < Gj; j++){
            cin >> num;
            grid_row[j] = num;
            lock_row[j] = 0;
            pthread_mutex_init(&mutex_row[j],NULL); // Initialize the mutex grid
            pthread_mutex_init(&ss_mutex_row[j],NULL); // Initialize the mutex grid
        }
        ciggbuts_grid.push_back(grid_row);
        lock_list.push_back(mutex_row);
        ss_lock_list.push_back(ss_mutex_row);
        locked.push_back(lock_row);
    }

    int Np; // Number of pp
    vector<pthread_t> pthread_list;
    vector<ProperPrivate *> pp_list;
    cin >> Np;
    total_privates = Np;
    
    for(int k = 0; k < Np; k++){
        pthread_t t;
        int gid_;
        int si_;
        int sj_;
        int tg_;
        int ng_;


        cin >> gid_; 
        cin >> si_; 
        cin >> sj_;
        cin >> tg_;
        cin >> ng_;



        vector<int> area_size_ = {si_,sj_};
        vector<vector<int>> to_do_;
        for(int t = 0; t < ng_; t++){
            vector<int> area_(2);
            cin >> area_[0];
            cin >> area_[1];
            to_do_.push_back(area_);
        }
        ProperPrivate* pp = new ProperPrivate(area_size_, to_do_, gid_, tg_, debug_p);  // Construct a PP;
        pp_list.push_back(pp);

        pthread_create(&t,NULL,hw2_phase1_begin_wrapper,(void *) pp);

        pthread_list.push_back(t);
    }

    // Phase 2
    pthread_t com_t;
    int order_number;
    PHASE_2 = scanf("%d",&order_number);

    if(PHASE_2 != EOF){
        vector<int> orders_t(order_number);
        vector<string> orders_string(order_number);

        for(int i = 0; i < order_number; i++){ // Set up the orders
            int order_t;
            string order;

            cin >> order_t;
            cin >> order;

            orders_t[i] = order_t;
            orders_string[i] = order;
        }

        Commander *com = new Commander(orders_t,orders_string,order_number);
        pthread_create(&com_t,NULL,hw2_phase2_begin_wrapper,(void *) com);

    }

    // PHASE 3

        vector<SneakySmoker *> ss_list;
        int Ns;
        PHASE_3 = scanf("%d",&Ns);
        if(PHASE_3 != EOF){
            for(int i = 0; i < Ns; i++){
                int sid; // smoker id
                int ts; // smoker ts 
                int saa; // smoker area amount

                pthread_t t;

                cin >> sid;
                cin >> ts;
                cin >> saa;

                vector<vector<int>> smoker_to_do;
                vector<int> smoker_ciggs;

                for(int j = 0; j < saa; j++){
                    vector<int> area(2);
                    int start_i;
                    int start_j;

                    cin >> start_i;
                    cin >> start_j;

                    area[0] = start_i;
                    area[1] = start_j;

                    smoker_to_do.push_back(area);
                    
                    int cigg_count;
                    
                    cin >> cigg_count;

                    smoker_ciggs.push_back(cigg_count);
                }


                SneakySmoker *ss = new SneakySmoker(smoker_ciggs,smoker_to_do,sid,ts,debug_s);
                ss_list.push_back(ss);

                pthread_create(&t,NULL,hw2_phase3_begin_wrapper,(void *) ss);

                pthread_list.push_back(t);
            }
        }


    // Join threads
    for(int i = 0; i < pthread_list.size(); i++){
        pthread_join(pthread_list[i],NULL);
    }
    
    return 0;
    
}