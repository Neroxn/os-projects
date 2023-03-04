#include "fcntl.h"
#include "unistd.h"
#include "fat32.h"
#include "parser.h"

#include <string>
#include <iostream>
#include <vector>
#include <stack>
#include <queue>
#include <ctime>

using namespace std;

#define INTS sizeof(int) 
#define BPBS sizeof(BPB_struct)
#define ROOT_DIRECTORY 2
#define END_CLUSTER 0x0FFFFFF8
class FAT_Block{
    /*
        Structure for the FAT block in the file system.
        To read a block, we have to open a file with descriptor fd.
        BPB_struct will be assigne throughout this process.
    */
    BPB_struct bpb; // Holds the information about BPB.
    int fd = -1;
    unsigned int fat_start_offset; // Reserved sector should be skipped to reach the offset
    
    public:
        FAT_Block(BPB_struct &bpb_, int fd_){ // Can construct a FAT_block structure if we have bpb. Also provide file descriptor
                                              // so that we can read from a fat block as well.
            bpb = bpb_;                 
            fd = fd_;
            set_start_offset();
        }

        // Set- get method defined for the offset
        void set_start_offset(){
            uint16_t bps = bpb.BytesPerSector;
            uint16_t rsc = bpb.ReservedSectorCount;
            fat_start_offset = bps*rsc;
        }

        unsigned int get_start_offset(){
            return fat_start_offset;
        }

        unsigned int get_fat_table_size(){
            return bpb.BytesPerSector * bpb.extended.FATSize;
        }
        void write_to_fat(int index, int value){ 
            int start = get_start_offset();
            int true_offset = start + index*INTS; 
            int cluster_id;

            // Since there can be multiple file allocation table, update the value of FAT for (FAT table times)
            int fat_table_many = bpb.NumFATs;
            for(int i = 0; i < fat_table_many; i++){
                lseek(fd, true_offset, 0); // or use SEEK_SET
                write(fd, &value, INTS); 

                // Update the offset by skippnig a fat table size
                true_offset += get_fat_table_size(); 

            }
            return;
        }

        unsigned int get_from_fat(int index){
            int start = get_start_offset();
            int true_offset = start + index*INTS; 
            int cluster_id;

            lseek(fd, true_offset, 0); // or use SEEK_SET
            read(fd, &cluster_id, INTS); // read integer and get the cluster number
		    return cluster_id & 0x0fffffff; // upper 4 bits should be masked since in FAT32 -> 28 bytes are used for
										    // each cluster
        }



};

class DATA_Block{
    /*
        Structure for reading or writing on the DATA block in the FAT filesystem.
    */
    BPB_struct bpb; // Holds the information about BPB.
    int fd = -1;
    unsigned int data_start_offset = 0; // Reserved sector should be skipped to reach the offset

    public:
        DATA_Block(BPB_struct &bpb_, int fd_){ // Can construct a FAT_block structure if we have bpb. Also provide file descriptor
                                              // so that we can read from a fat block as well.
            bpb = bpb_;                 
            fd = fd_;
            set_start_offset();
        }

        void set_start_offset(){ // Set the offset where the datablock starts.
            // Skip the reserved sectors
            uint16_t bps = bpb.BytesPerSector;
            uint16_t rsc = bpb.ReservedSectorCount;
            data_start_offset += bps*rsc;

            // Skip the FAT sectors
            uint32_t spf = bpb.extended.FATSize; // sector_per_fat
            uint8_t nf = bpb.NumFATs; // num fats
            data_start_offset += spf * bps * nf;
        }

        unsigned int get_start_offset(){
            return data_start_offset;
        }

        unsigned int get_cluster_size(){
            uint16_t bps = bpb.BytesPerSector;
            uint16_t spc = bpb.SectorsPerCluster;
            return  bps * spc;
        }

        void write_to_dblock(int index, void *data){ // *data should point to a cluster-sized data.
            unsigned cluster_size = get_cluster_size();

            unsigned int dblock_offset = get_start_offset();
            dblock_offset += (index - 2) * cluster_size; // root starts from cluster index 2

            lseek(fd,dblock_offset,0); // go to  the cluster
            int s = write(fd,data,cluster_size); // write new cluster data to cluster
            //cout << "s : " << s << endl;
        }

        void* get_from_dblock(int index){ // Basically, read the cluster
            unsigned cluster_size = get_cluster_size();

            void *cluster_ptr = new char[cluster_size];

            unsigned int dblock_offset = get_start_offset();
            dblock_offset += (index - 2) * cluster_size; // root starts from cluster index 2
            
            lseek(fd,dblock_offset,0); // go to  the cluster
            read(fd,cluster_ptr,cluster_size); // read the cluster

            return cluster_ptr;
        }

};

// Methods for CD.
void set_starting_cluster(int &cur_clus, const char *curr_path){
    int is_absolute = (curr_path[0] == '/');
    if(is_absolute){
        cur_clus = ROOT_DIRECTORY;
    }
}

void set_starting_directory(string &cur_dic, const char *curr_path){
    int is_absolute = (curr_path[0] == '/');
    if(is_absolute){
        cur_dic = "/";
    }
}

void set_paths(vector<string> &paths, const char *curr_path){
    // curr_path is the full path name
    // either /a/b/c or b/c. 
    int END_REACHED = 0;
    int is_absolute = (curr_path[0] == '/');

    string path = "";
    int i = 0;
    if(is_absolute){
        i++;
    }
    while(!END_REACHED){
        if(curr_path[i] == '\0'){
            END_REACHED = 1;
        }
        else if(curr_path[i] == '/'){
            //cout << "Path added : " << path << endl;
            paths.push_back(path);
            path = "";
        }
        else{
            path += curr_path[i];
        }
        i++;
    }
    //cout << "Path added : " << path << endl;
    paths.push_back(path);
}

void set_current_parent(void * clstr_ptr, int &cur_clus){
    // Basic pointer calculation. Second Fat83 entry 
    // in a cluster is the parent directory entry.
    FatFile83* cluster83 = (FatFile83 *) clstr_ptr;
    FatFile83 parent_entry = *(cluster83 + 1);
    
    // Now we have the entry, find its first cluster
    // eaIndex << 16 | last16Bit
    cur_clus = parent_entry.eaIndex << 16 | parent_entry.firstCluster;

    if(cur_clus == 0){
        cur_clus = 2;
    }

}

int cd_(string &destination,string &starting_directory, int &starting_cluster, DATA_Block &dblock, FAT_Block &fblock){
    // Implementation of CD command. CD takes exactly one argument.
    /*
    1: procedure CD(path)
        2: Locate(path)
        3: if returns error then
            4: Return
        5: else
            6: Record return info for future use
            7: Update prompt
            8: Return
        9: end if
    10: end procedure
    */

    string current_path = starting_directory;
    vector<string> paths;

    int current_cluster = starting_cluster;
    int DESTINATION_NOT_REACHED = 1;
    int path_count = 0;
    int total_fat_entries = dblock.get_cluster_size() / sizeof(FatFile83);
    // Set current cluster and path with respect to the whether path is absolute or not
    set_starting_cluster(current_cluster, destination.c_str());
    set_starting_directory(current_path, destination.c_str());

    //cout << "Currents : " << current_path << current_cluster  << endl;
    // Parse path into set of directories for traversing
    set_paths(paths, destination.c_str()); // example : paths/testdir/dir -> {"paths","testdir","dir"}
                                            // /paths/test/dir -> paths test dir as well
    //cout << "Patrh length : " << paths.size() << endl;
    while(DESTINATION_NOT_REACHED && path_count < paths.size()){
        string next_path = paths[path_count];

        //cout << "Target is :" << next_path << endl;
        if(next_path == "."){ // Do nothing
            path_count++;
            continue;
        }
        else if(next_path == ".."){
            // Must find the parent. If root, return error
            if(current_path == "/"){
                //std::cout << "Root has no parent" << std::endl;
                DESTINATION_NOT_REACHED = 0;
                path_count++;
                continue;
            }

            // Read the current block. 
            void *cluster_pointer = dblock.get_from_dblock(current_cluster);
            set_current_parent(cluster_pointer, current_cluster);
            //cout << "Parent is set" << current_cluster << endl;
            // Update current path
            // a/b/c -> a/b so we have to remove /c
            int BACKSLASH_DETECTED = 0;
            while(!BACKSLASH_DETECTED){
                //cout << current_path << endl;
                char last = current_path.back();

                if(last == '/'){    
                    BACKSLASH_DETECTED = 1;
                    if(current_cluster != 2){
                        current_path.pop_back();
                    }
                }
                else{
                    current_path.pop_back();
                    }
            }

            //cout << current_path << endl;
            path_count++;
            continue;

        }
        else{  // Else, traverse the all cluster and check
               // each entries and the target path.
               // LFN comes first!
            int break_loop = 0;
            vector<FatFileLFN*> lfn_vec;

            for(int traverse_cluster = current_cluster; traverse_cluster < END_CLUSTER; traverse_cluster = fblock.get_from_fat(traverse_cluster)){
                void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
                // LFN entries come first.
                int i = 0;
                for(; i < total_fat_entries; i++){    
                    // If not the root, we have to start from 2, else start from 0
                    FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
                    traverse_pointer += i;

                    if(traverse_pointer->sequence_number == 0x00){
                        free(cluster_pointer);
                        return -1;
                    }
                    else if(traverse_pointer->sequence_number == 0xE5){
                        continue;
                    }

                   else if(traverse_pointer->sequence_number == 0x2E){
                        continue;
                    }
                    lfn_vec.push_back(traverse_pointer);
                    int next_true_entry = (static_cast<int>(traverse_pointer->sequence_number) == 1) || (static_cast<int>(traverse_pointer->sequence_number) == 65);
                    //cout << " i : " << i << " lfn "  << next_true_entry << endl; 
                    if(next_true_entry){
                        //Now we are at true FatFile83 directory entry
                        //Extract the name from the stack
                        // Concatanate them to get the filneame
                        // Compare it with the target
                        // If true, calculate the next cluster index
                        // Go to that cluster and continue looping!
                        FatFile83 *true_entry = (FatFile83 *) cluster_pointer;
                        i++;
                        true_entry += i;
                        //true_entry += (i+1);
                        string concat_file_name = "";
                        next_true_entry = 0;
                        int end_reached = 0;

                        //cout << " --> " << lfn_vec.size() << endl;
                        for(int i2 = 1; i2 < lfn_vec.size(); i2++){
                            FatFileLFN* dummy = lfn_vec[i2];
                            for(int j = 0; j < 5; j++){
                                if(dummy->name1[j] == '\0'){
                                    end_reached = 1;
                                }   
                                if(end_reached){
                                    break;
                                }
                                concat_file_name += dummy->name1[j]; 

                            }
                            for(int j = 0; j < 6; j++){
                                if(dummy->name2[j] == '\0'){
                                    end_reached = 1;
                                }   
                                if(end_reached){
                                    break;
                                }    
                                concat_file_name += dummy->name2[j];    
                            }
                            for(int j = 0; j < 2; j++){
                                if(dummy->name3[j] == '\0'){
                                    end_reached = 1;
                                }   
                                if(end_reached){
                                    break;
                                }   
                                concat_file_name += dummy->name3[j];    
                            }
                        }
                        FatFileLFN* dummy = lfn_vec[0];
                        for(int j = 0; j < 5; j++){ 
                            if(dummy->name1[j] == '\0'){
                                end_reached = 1;
                            }   
                            if(end_reached){
                                break;
                            } 
                            concat_file_name += dummy->name1[j];    
                        }

                        for(int j = 0; j < 6; j++){
                            if(dummy->name2[j] == '\0'){
                                end_reached = 1;
                            }   
                            if(end_reached){
                                break;
                            } 
                            concat_file_name += dummy->name2[j];    
                        }

                        for(int j = 0; j < 2; j++){
                            if(dummy->name3[j] == '\0'){
                                end_reached = 1;
                            }   
                            if(end_reached){
                                break;
                            } 
                            concat_file_name += dummy->name3[j];    
                        }

                        while(lfn_vec.size()){ // remove each element
                            lfn_vec.pop_back();
                        }

                        //cout  << concat_file_name << " vs " << next_path << endl;
                        if(concat_file_name == next_path){
                            current_cluster = (true_entry->eaIndex << 16) | true_entry->firstCluster;

                            if(current_path != "/"){
                                current_path += "/";
                            }
                            current_path += concat_file_name;
                            break_loop = 1;

                        }

                    }
                    if(break_loop){
                        break;
                    }
                }
                if(break_loop){
                    break;
                }
            }
        }

        path_count++;
        if(path_count == paths.size()) {
            DESTINATION_NOT_REACHED = 0;
        }
    }
    starting_cluster = current_cluster;
    starting_directory = current_path;
    return 0;

}


int cd_modify(string &destination,string &starting_directory, int &starting_cluster, DATA_Block &dblock, FAT_Block &fblock){
    // Implementation of CD command. CD takes exactly one argument.
    /*
    1: procedure CD(path)
        2: Locate(path)
        3: if returns error then
            4: Return
        5: else
            6: Record return info for future use
            7: Update prompt
            8: Return
        9: end if
    10: end procedure
    */

    string current_path = starting_directory;
    vector<string> paths;

    int current_cluster = starting_cluster;
    int DESTINATION_NOT_REACHED = 1;
    int path_count = 0;
    int total_fat_entries = dblock.get_cluster_size() / sizeof(FatFile83);
    // Set current cluster and path with respect to the whether path is absolute or not
    set_starting_cluster(current_cluster, destination.c_str());
    set_starting_directory(current_path, destination.c_str());

    //cout << "Currents : " << current_path << current_cluster  << endl;
    // Parse path into set of directories for traversing
    set_paths(paths, destination.c_str()); // example : paths/testdir/dir -> {"paths","testdir","dir"}
                                            // /paths/test/dir -> paths test dir as well
    //cout << "Patrh length : " << paths.size() << endl;
    while(DESTINATION_NOT_REACHED && path_count < paths.size()){
        string next_path = paths[path_count];

        //cout << "Target is :" << next_path << endl;
        if(next_path == "."){ // Do nothing
            path_count++;
            continue;
        }
        else if(next_path == ".."){
            // Must find the parent. If root, return error
            if(current_path == "/"){
                //std::cout << "Root has no parent" << std::endl;
                DESTINATION_NOT_REACHED = 0;
                path_count++;
                continue;
            }

            // Read the current block. 
            void *cluster_pointer = dblock.get_from_dblock(current_cluster);
            set_current_parent(cluster_pointer, current_cluster);
            //cout << "Parent is set" << current_cluster << endl;
            // Update current path
            // a/b/c -> a/b so we have to remove /c
            int BACKSLASH_DETECTED = 0;
            while(!BACKSLASH_DETECTED){
                //cout << current_path << endl;
                char last = current_path.back();

                if(last == '/'){    
                    BACKSLASH_DETECTED = 1;
                    if(current_cluster != 2){
                        current_path.pop_back();
                    }
                }
                else{
                    current_path.pop_back();
                    }
            }

            //cout << current_path << endl;
            path_count++;
            continue;

        }
        else{  // Else, traverse the all cluster and check
               // each entries and the target path.
               // LFN comes first!
            int break_loop = 0;
            vector<FatFileLFN*> lfn_vec;

            for(int traverse_cluster = current_cluster; traverse_cluster < END_CLUSTER; traverse_cluster = fblock.get_from_fat(traverse_cluster)){
                void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
                // LFN entries come first.
                int i = 0;
                for(; i < total_fat_entries; i++){    
                    // If not the root, we have to start from 2, else start from 0
                    FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
                    traverse_pointer += i;

                    if(traverse_pointer->sequence_number == 0x00){
                        free(cluster_pointer);
                        return -1;
                    }
                    else if(traverse_pointer->sequence_number == 0xE5){
                        continue;
                    }

                   else if(traverse_pointer->sequence_number == 0x2E){
                        continue;
                    }
                    lfn_vec.push_back(traverse_pointer);
                    int next_true_entry = (static_cast<int>(traverse_pointer->sequence_number) == 1) || (static_cast<int>(traverse_pointer->sequence_number) == 65);
                    if(next_true_entry){
                        //Now we are at true FatFile83 directory entry
                        //Extract the name from the stack
                        // Concatanate them to get the filneame
                        // Compare it with the target
                        // If true, calculate the next cluster index
                        // Go to that cluster and continue looping!
                        FatFile83 *true_entry = (FatFile83 *) cluster_pointer;
                        true_entry += (i+1);
                        string concat_file_name = "";
                        next_true_entry = 0;
                        int end_reached = 0;
                        for(int i2 = 1; i2 < lfn_vec.size(); i2++){
                            FatFileLFN* dummy = lfn_vec[i2];
                            for(int j = 0; j < 5; j++){
                                if(dummy->name1[j] == '\0'){
                                    end_reached = 1;
                                }   
                                if(end_reached){
                                    break;
                                }
                                concat_file_name += dummy->name1[j]; 

                            }

                            for(int j = 0; j < 6; j++){
                                if(dummy->name2[j] == '\0'){
                                    end_reached = 1;
                                }   
                                if(end_reached){
                                    break;
                                }    
                                concat_file_name += dummy->name2[j];    
                            }

                            for(int j = 0; j < 2; j++){
                                if(dummy->name3[j] == '\0'){
                                    end_reached = 1;
                                }   
                                if(end_reached){
                                    break;
                                }   
                                concat_file_name += dummy->name3[j];    
                            }
                        }
                        FatFileLFN* dummy = lfn_vec[0];
                        for(int j = 0; j < 5; j++){ 
                            if(dummy->name1[j] == '\0'){
                                end_reached = 1;
                            }   
                            if(end_reached){
                                break;
                            } 
                            concat_file_name += dummy->name1[j];    
                        }

                        for(int j = 0; j < 6; j++){
                            if(dummy->name2[j] == '\0'){
                                end_reached = 1;
                            }   
                            if(end_reached){
                                break;
                            } 
                            concat_file_name += dummy->name2[j];    
                        }

                        for(int j = 0; j < 2; j++){
                            if(dummy->name3[j] == '\0'){
                                end_reached = 1;
                            }   
                            if(end_reached){
                                break;
                            } 
                            concat_file_name += dummy->name3[j];    
                        }

                        while(lfn_vec.size()){ // remove each element
                            lfn_vec.pop_back();
                        }
                        if(concat_file_name == next_path){
                            // F83 is the parent directory
                            time_t current_time = std::time(0);
                            struct tm * time_struct = std::localtime(&current_time);
                            true_entry->modifiedTime = (time_struct->tm_hour << 11) | (time_struct->tm_min << 5) | (time_struct->tm_sec / 2);
                            true_entry->modifiedDate = ((time_struct->tm_year - 80) << 9) | ((time_struct->tm_mon) << 5) | time_struct->tm_mday;
                            FatFile83 *entry_ptr = (FatFile83 *) cluster_pointer;
                            *(entry_ptr + i + 1)  = *true_entry;
                            //cout << " Date is updated !";
                            dblock.write_to_dblock(traverse_cluster,cluster_pointer);
                            void *temp = dblock.get_from_dblock(traverse_cluster);
                            entry_ptr = (FatFile83 *) temp;
                            //cout << (entry_ptr + i + 1)->modifiedTime << endl;
                            //cout << true_entry->modifiedTime << endl;
                            break_loop = 1;
                            break;
                        }

                    }
                    if(break_loop){
                        break;
                    }
                }
                if(break_loop){
                    break;
                }
            }
        }

        path_count++;
        if(path_count == paths.size()) {
            DESTINATION_NOT_REACHED = 0;
        }
    }
    starting_cluster = current_cluster;
    starting_directory = current_path;
    return 0;

}

void produce_detailed_output(string header,int file_size,int min,int hour,int day,string month,string concat_file_name){
    string min_start = "";
    string day_start = "";
    string hour_start = "";
    string space = " ";
    
    if(day < 10){
        day_start = "0";
    }
    if(hour < 10){
        hour_start = "0";
    }
    if(min < 10){
        min_start = "0";
    }


    cout << header << file_size << space << month << space << day_start << day << space  <<  \
            hour_start << hour << ":" << min_start << min << \
            space << concat_file_name << endl;
}

void set_date(int &min, int &hour, int &day, int date, int time, string &month ){
    day = date & 0b0000000000011111;
    hour = ((time & 0b1111100000000000) >> 11);
    min = ((time & 0b0000011111100000) >> 5);

    // Convert month
    int monthid = (date & 0b0000000111100000) >> 5;
    monthid += 1;
    if(monthid == 1){
        month = "January";
    }
    else if(monthid == 2){
        month = "February"; 
    }
    else if(monthid == 3){
        month = "March";

    }    
    else if(monthid == 4){
        month = "April";
    }
    else if(monthid == 5){
        month = "May";
    }
    else if(monthid == 6){
        month = "June";
    }
    else if(monthid == 7){
        month = "July";
    } 
    else if(monthid == 8){
        month = "August";
    }
    else if(monthid == 9){
        month = "September";
    }
    else if(monthid == 10){
        month = "October";
    }
    else if(monthid == 11){
        month = "November";
    } 
    else{
        month = "December";
    }
}

void cd(parsed_input *pinput,string &starting_directory, int &starting_cluster, DATA_Block &dblock, FAT_Block &fblock){
    // cd is wrapped
    string destination = pinput->arg1;
    cd_(destination,starting_directory,starting_cluster,dblock,fblock);
}

void ls(parsed_input *pinput, string starting_directory, int starting_cluster, DATA_Block &dblock, FAT_Block &fblock){
    // We dont have to change starting_directory and starting_cluster. Using a "copy" instead of referance
    // we can set this up.

    // arg1 can take -l for extended information, arg2
    // if -l provided, it will be always before the path! So if arg2 exist, arg1 is -l
    // if arg1 exist, it is either -l for the current directory or the path
    string arg1 = "";
    string arg2 = "";
    string fileHeader = "-rwx------ 1 root root ";
    string directoryHeader = "drwx------ 1 root root ";
    string path;
    
    string current_directory = starting_directory;
    int current_cluster = starting_cluster;


    int total_fat_entries = dblock.get_cluster_size() / sizeof(FatFile83);
    int l_flag;

    if(pinput->arg1){
        arg1 = string(pinput->arg1);
    }
    if(pinput->arg2){
        arg2 = string(pinput->arg2);
    }
    //cout << "arg1 " << arg1 << " arg2" << arg2 << endl;

    if(arg2 != ""){ // ls -l path
        path = arg2;
        l_flag = 1;
    }
    else{
        if(arg1 == ""){ // ls     
            path = ".";
            l_flag = 0;
        }
        else{ // ls -l or ls path 
            if(arg1 == "-l"){
                path = ".";
                l_flag = 1;
            }
            else{ // ls  path
                path = arg1;
                l_flag = 0;
            }
        }
    }


    // CD into directory if possible
    int path_exist = cd_(path,current_directory,current_cluster,dblock,fblock);

    if(path_exist == -1){
        return;
    }

    int break_loop = 0;
    vector<FatFileLFN*> lfn_vec;

    for(int traverse_cluster = current_cluster; traverse_cluster < END_CLUSTER; traverse_cluster = fblock.get_from_fat(traverse_cluster)){
        void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
        // LFN entries come first.
        int i = 0;
        for(; i < total_fat_entries; i++){    
            // If not the root, we have to start from 2
            FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
            traverse_pointer += i;
            if(traverse_pointer->sequence_number == 0x00){
                free(cluster_pointer);
                if(!l_flag){
                    cout << endl;
                }
                return;
            }
            else if(traverse_pointer->sequence_number == 0xE5){
                continue;
            }
            else if(traverse_pointer->sequence_number == 0x2E){
                continue;
            }

            lfn_vec.push_back(traverse_pointer);
            //std::cout << "Hex : " << std::hex << static_cast<int>(traverse_pointer->sequence_number) << std::endl;
            //cout << " NTE : " << (static_cast<int>(traverse_pointer->sequence_number) == 1) << endl;
            //cout << " NTE 2 : " << (static_cast<int>(traverse_pointer->sequence_number) == 65) << endl;
            
            //int next_true_entry = ((traverse_pointer->sequence_number & 0x1) == 1);
            
            int next_true_entry = (static_cast<int>(traverse_pointer->sequence_number) == 1) || (static_cast<int>(traverse_pointer->sequence_number) == 65);
            if(next_true_entry){
                //Now we are at true FatFile83 directory entry
                //Extract the name from the stack
                // Concatanate them to get the filneame
                // Compare it with the target
                // If true, calculate the next cluster index
                // Go to that cluster and continue looping
                i += 1;
                FatFile83 *true_entry = (FatFile83 *) cluster_pointer;
                true_entry += i;
                string concat_file_name = "";
                next_true_entry = 0;
                int end_reached = 0;
                for(int i2 = 1; i2 < lfn_vec.size(); i2++){
                    FatFileLFN* dummy = lfn_vec[i2];
                    for(int j = 0; j < 5; j++){
                        if(dummy->name1[j] == '\0'){
                            end_reached = 1;
                        }   
                        if(end_reached){
                            break;
                        }
                        concat_file_name += dummy->name1[j]; 

                    }

                    for(int j = 0; j < 6; j++){
                        if(dummy->name2[j] == '\0'){
                            end_reached = 1;
                        }   
                        if(end_reached){
                            break;
                        }    
                        concat_file_name += dummy->name2[j];    
                    }

                    for(int j = 0; j < 2; j++){
                        if(dummy->name3[j] == '\0'){
                            end_reached = 1;
                        }   
                        if(end_reached){
                            break;
                        }   
                        concat_file_name += dummy->name3[j];    
                    }
                }
                FatFileLFN* dummy = lfn_vec[0];
                for(int j = 0; j < 5; j++){ 
                    if(dummy->name1[j] == '\0'){
                        end_reached = 1;
                    }   
                    if(end_reached){
                        break;
                    } 
                    concat_file_name += dummy->name1[j];    
                }

                for(int j = 0; j < 6; j++){
                    if(dummy->name2[j] == '\0'){
                        end_reached = 1;
                    }   
                    if(end_reached){
                        break;
                    } 
                    concat_file_name += dummy->name2[j];    
                }

                for(int j = 0; j < 2; j++){
                    if(dummy->name3[j] == '\0'){
                        end_reached = 1;
                    }   
                    if(end_reached){
                        break;
                    } 
                    concat_file_name += dummy->name3[j];    
                }

                while(lfn_vec.size()){ // remove each element
                    lfn_vec.pop_back();
                }

                //cout << "Files : " << concat_file_name  << endl;
                if(l_flag){

                    // Set date with bits
                    string month;
                    int min;
                    int hour;
                    int day;

                    int date = true_entry->modifiedDate;
                    int time = true_entry->modifiedTime;

                    set_date(min,hour,day,date,time,month);

                    int is_directory = true_entry->attributes == 0x10;
                    string header;
                    int file_size;
                    if(is_directory){
                        header = directoryHeader;
                        file_size = 0;
                    }
                    else{
                        header = fileHeader;
                        file_size = true_entry -> fileSize;
                    }
                    produce_detailed_output(header,file_size,min,hour,day,month, concat_file_name);
                }
                else{
                    cout << concat_file_name << " ";
                }
            }
            if(break_loop){
                break;
            }
        }
        if(break_loop){
            break;
        }
    }

}

void string_to_c_str(string &string_, char* char_){
    int i = 0;
    for(; i < string_.size(); i++){

        char_[i] = string_[i];
    }
    char_[i] = '\0';
}


// CAT

void read_cluster(int current_cluster, FAT_Block &fblock, DATA_Block &dblock){
    int END_REACHED = 0;
    int cluster_size = dblock.get_cluster_size();
    while(current_cluster < 0x0FFFFFF8){
        void* cluster_pointer = dblock.get_from_dblock(current_cluster); // get the cluster
        char* character_pointer = (char *) cluster_pointer;


        //read all cluster with character pointer
        for(int i = 0; i < cluster_size; i++){
            char current = *(character_pointer + i);
            if(current == -1){ // EOF
                cout << endl;
                END_REACHED = 1;
                return;
            }
            
            cout << current ;

        }

        current_cluster = fblock.get_from_fat(current_cluster);

    }
}

void seperate_path_file(string &path, string &file, string arg1){
    int last_backward_slash = -1;
    int first_backward_slash = -1;
    for(int i = 0; i < arg1.size(); i++){
        if(arg1[i] == '/'){
            last_backward_slash = i;
            if(first_backward_slash == -1){
                first_backward_slash = i;
            }
        }
    }

    // if path /b   , b is file and / is path
    // if path /b/c, c is file and /b is path
    // if path b/c,  c is file and b is path and c is file  
    // if path b  , b is file and path is ./

    int file_name_size = path.size() - last_backward_slash;

    for(int i = 0; i < last_backward_slash; i++){
        path += arg1[i];
    }
    for(int i = last_backward_slash+1; i < arg1.size(); i++){
        file += arg1[i];
    }

    // Case 4
    if(path == ""){
        path = ".";
    }

    
}
void cat(parsed_input *pinput, string starting_directory, int starting_cluster, DATA_Block &dblock, FAT_Block &fblock){
    // From the pinput, get the path + file name.
    // CD into path
    // Read the file if exist

    string current_directory = starting_directory;
    int current_cluster = starting_cluster;

    int total_fat_entries = dblock.get_cluster_size() / sizeof(FatFile83);

    string path;
    string file_name;
    
    string arg1 = string(pinput->arg1);

    seperate_path_file(path,file_name,arg1);

    // We have both path and file.

    // CD into directory if possible
    int path_exist = cd_(path,current_directory,current_cluster,dblock,fblock);

    if(path_exist == -1){
        return;
    } 

    // Traverse each file. When the name is equal to the file_name, read it and output!
    int break_loop = 0;
    vector<FatFileLFN*> lfn_vec;

    for(int traverse_cluster = current_cluster; traverse_cluster < END_CLUSTER; traverse_cluster = fblock.get_from_fat(traverse_cluster)){
        void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
        // LFN entries come first.
        int i = 0;
        if(current_cluster != ROOT_DIRECTORY && current_cluster == traverse_cluster ){
            i = 2;
        }
        for(; i < total_fat_entries; i++){    
            // If not the root, we have to start from 2, else start from 0
            FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
            traverse_pointer += i;

            if(traverse_pointer->sequence_number == 0x00){
                free(cluster_pointer);
                return ;
            }
            else if(traverse_pointer->sequence_number == 0xE5){
                continue;
            }
            else if(traverse_pointer->sequence_number == 0x2E){
                continue;
            }

            else if(traverse_pointer)
            lfn_vec.push_back(traverse_pointer);
            int next_true_entry = (static_cast<int>(traverse_pointer->sequence_number) == 1) || (static_cast<int>(traverse_pointer->sequence_number) == 65);
            
            //cout << " i : " << i << " lfn "  << !next_true_entry << endl; 
            if(next_true_entry){
                //Now we are at true FatFile83 directory entry
                //Extract the name from the stack
                // Concatanate them to get the filneame
                // Compare it with the target
                // If true, calculate the next cluster index
                // Go to that cluster and continue looping!
                //cout << "Index : i -> " << i << endl;
                FatFile83 *true_entry = (FatFile83 *) cluster_pointer;
                true_entry += (i+1);
                string concat_file_name = "";
                next_true_entry = 0;
                int end_reached = 0;
                
                for(int i2 = 1; i2 < lfn_vec.size(); i2++){
                    FatFileLFN* dummy = lfn_vec[i2];
                    for(int j = 0; j < 5; j++){
                        if(dummy->name1[j] == '\0'){
                            end_reached = 1;
                        }   
                        if(end_reached){
                            break;
                        }
                        concat_file_name += dummy->name1[j]; 

                    }

                    for(int j = 0; j < 6; j++){
                        if(dummy->name2[j] == '\0'){
                            end_reached = 1;
                        }   
                        if(end_reached){
                            break;
                        }    
                        concat_file_name += dummy->name2[j];    
                    }

                    for(int j = 0; j < 2; j++){
                        if(dummy->name3[j] == '\0'){
                            end_reached = 1;
                        }   
                        if(end_reached){
                            break;
                        }   
                        concat_file_name += dummy->name3[j];    
                    }
                }
                FatFileLFN* dummy = lfn_vec[0];
                for(int j = 0; j < 5; j++){ 
                    if(dummy->name1[j] == '\0'){
                        end_reached = 1;
                    }   
                    if(end_reached){
                        break;
                    } 
                    concat_file_name += dummy->name1[j];    
                }

                for(int j = 0; j < 6; j++){
                    if(dummy->name2[j] == '\0'){
                        end_reached = 1;
                    }   
                    if(end_reached){
                        break;
                    } 
                    concat_file_name += dummy->name2[j];    
                }

                for(int j = 0; j < 2; j++){
                    if(dummy->name3[j] == '\0'){
                        end_reached = 1;
                    }   
                    if(end_reached){
                        break;
                    } 
                    concat_file_name += dummy->name3[j];    
                }

                while(lfn_vec.size()){ // remove each element
                    lfn_vec.pop_back();
                }

                if(true_entry->attributes == 0x10){
                    continue; // if directory, continue
                }

                //cout << "Files : " << concat_file_name << endl;
                //cout << concat_file_name.size() << next_path.size() << endl;
                if(concat_file_name == file_name){
                    // we have the entry
                    //cout << "Current cluster was" << current_cluster  << endl;

                    //cout << true_entry->eaIndex << " and " << true_entry->firstCluster << endl;
                    current_cluster = (true_entry->eaIndex << 16) + true_entry->firstCluster;
                    if(current_cluster == 0){
                        current_cluster = 2;
                    }
                    //cout << "Current cluster is" << current_cluster  << endl;
                    // First cluster is found. Read the content and switch cluster with FAT table!
                    read_cluster(current_cluster, fblock, dblock);
                    break_loop = 1;

                }

            }
            if(break_loop){
                break;
            }
        }
        if(break_loop){
            break;
        }
    }

}

int allocate_free_cluster(DATA_Block &dblock, FAT_Block &fblock){
    int EMPTY_FOUND = 0;
    int i = 0;
    while(!EMPTY_FOUND){
        if(fblock.get_from_fat(i) == 0){
            EMPTY_FOUND = 1;
            break;
        }
        i++;
    }
    return i;

}


unsigned char lfn_checksum(unsigned char *fname)
{
   int i;
   unsigned char sum = 0;

   for (i = 11; i; i--)
      sum = ((sum & 1) << 7) + (sum >> 1) + *fname++;

   return sum;
}

 
void create_lfn_list(vector<FatFileLFN> &lfn_list,string folder_name){

    int lfn_size = lfn_list.size();
    unsigned char *fname = new unsigned char[lfn_size + 1];
    for(int i = 0; i < lfn_size; i++){
        fname[i] = folder_name[i];
    }
    fname[lfn_size] = '\0';
    unsigned checksum = lfn_checksum(fname);
    int i;
    for(i = 0; i < lfn_size - 1; i++){
        FatFileLFN* fat_file = new FatFileLFN();

        fat_file->sequence_number = lfn_size - (i+1);
        fat_file->attributes = 0x0F;
        fat_file->reserved = 0x00;
        fat_file->firstCluster = 0x0000;
        fat_file->checksum =checksum;

        for(int j = 0; j < 5; j++){
            //cout << "name1 -> " << j + 13*i << " written " << folder_name[j + 13*i] << endl;
            fat_file->name1[j] = folder_name[j + 13*i];

        }
        for(int j = 0; j < 6; j++){
            //cout << "name2 -> " << j + 5 + 13*i << " written " << folder_name[j + 5 + 13*i] << endl;
            fat_file->name2[j] = folder_name[j + 5 + 13*i];
                       
        }
        for(int j = 0; j < 2; j++){
            //cout << "name3 -> " << j + 11 + 13*i << " written " << folder_name[j + 11 + 13*i] << endl;
            fat_file->name3[j] = folder_name[j + 11 + 13*i];
        }
        // calculate checkum here
        lfn_list[i+1] = *(fat_file);

    }
    FatFileLFN* fat_file = new FatFileLFN();

    fat_file->sequence_number = 0x41 + lfn_size - 1;
    fat_file->attributes = 0x0F;
    fat_file->reserved = 0x00;
    fat_file->firstCluster = 0x0000;

    int character_left = (folder_name.size() % 13) + 1; 
    //cout << "Character left for lfn1 : " << character_left << endl;  
    if(character_left <= 13){
        for(int j = 0; j < 5; j++){
            if(character_left <= 0){
                break;
            }
            if(character_left-- == 1){
                fat_file->name1[j] = '\0';
            }
            fat_file->name1[j] = folder_name[j + 13*i];
        }
    }
    
    if(character_left <= 8){
        for(int j = 0; j < 6; j++){
            if(character_left <= 0){
                break;
            }
            if(character_left-- == 1){
                fat_file->name2[j] = '\0';
            }
            fat_file->name2[j] = folder_name[j + 5 + 13*i];
        }
    }
    if(character_left <= 2){
        for(int j = 0; j < 2; j++){
            if(character_left <= 0){
                break;
            }
            if(character_left-- == 1){
                fat_file->name3[j] = '\0';
            }
            fat_file->name3[j] = folder_name[j + 11 + 13*i];
        }
    }
    
    // calculate checkum here
    fat_file->checksum = checksum;
    lfn_list[0] = *(fat_file);
    
}


FatFile83 create_entry(int file_position, uint32_t new_cluster_index, int isFolder)
{

    FatFile83 directory_entry;
    memset(&directory_entry, 0x20, sizeof(FatFile83));
    string file_name;
    if(file_position == -1){ // .
        file_name = ".";
    }
    else if(file_position == 0){
        file_name = "..";
    }
    else{    
        file_name = "~" + to_string(file_position);
    }

    directory_entry.firstCluster = new_cluster_index & 0xFFFF;
    directory_entry.eaIndex = new_cluster_index & 0xFFFF0000;
    directory_entry.fileSize = 0;


    // copy the names
    for (int i = 0; i < file_name.size(); i++)
    {
        directory_entry.filename[i] = file_name[i];
    }


    if(isFolder){
        directory_entry.attributes = 0x10;
    }
    else{
        directory_entry.attributes = 0x20;
    }
    directory_entry.reserved = 0x0;

    time_t current_time = std::time(0);
    struct tm * time_struct = std::localtime(&current_time);
    directory_entry.creationTime = (time_struct->tm_hour << 11) | (time_struct->tm_min << 5) | (time_struct->tm_sec / 2);
    directory_entry.modifiedTime = directory_entry.creationTime;

    directory_entry.creationDate = ((time_struct->tm_year - 80) << 9) | ((time_struct->tm_mon) << 5) | time_struct->tm_mday;
    directory_entry.modifiedDate = directory_entry.creationDate;

    return directory_entry;
}


void mkdir(parsed_input *pinput, string starting_directory, int starting_cluster, DATA_Block &dblock, FAT_Block &fblock){
    // A function of mkdir. First traverse to the path 
    string current_directory = starting_directory;
    int current_cluster = starting_cluster;

    string temp_curr_directory = starting_directory;
    int temp_curr_cluster = starting_cluster;

    string temp2_curr_directory = starting_directory;
    int temp2_curr_cluster = starting_cluster;

    int total_fat_entries = dblock.get_cluster_size() / sizeof(FatFile83);

    string path;
    string folder_name;
    
    string path_parent;
    string folder_parent_name;

    string arg1 = string(pinput->arg1);

    seperate_path_file(path,folder_name,arg1);
    seperate_path_file(path_parent,folder_parent_name,path);


    // CD into directory if possible
    int path_exist = cd_(path,current_directory,current_cluster,dblock,fblock);

    if(path_exist == -1){
        return;
    }   

    int folder_exist = cd_(arg1,temp_curr_directory,temp_curr_cluster,dblock,fblock);

    if(folder_exist != -1){ // if folder exist
        return;
    }

    // So now we are at the current directory and current cluster. We have to create folder
    // Check whether there is a partial space in the cluster
    int entry_required   = folder_name.size()/13 + 2; // if less than 13 : 1 LFN + 1 83
    FatFileLFN* dest_pointer;
    // Traverse the cluster and the next clusters using FAT table
    int break_loop = 0;
    int directory_index = -1;
    int new_cluster_index = -1;
    int cluster_traveled = 0;
    int file_counter = 0;
    int traverse_cluster;
    int entry_index;
    for(traverse_cluster = current_cluster; traverse_cluster < END_CLUSTER; traverse_cluster = fblock.get_from_fat(traverse_cluster)){
        cluster_traveled++;
        void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
        vector<FatFileLFN*> lfn_vec;
        // LFN entries come first.
        int i = 0;
        file_counter = 0;
        for(; i < total_fat_entries; i++){    
            // If not the root, we have to start from 2, else start from 0
            FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
            traverse_pointer += i;
            int next_true_entry = (static_cast<int>(traverse_pointer->sequence_number) == 1) || (static_cast<int>(traverse_pointer->sequence_number) == 65);
            if(next_true_entry){
                file_counter++;
            }
            if(traverse_pointer->sequence_number == 0x00){
                // After this entry, all entries are availible. 
                dest_pointer = traverse_pointer ; // pointer to such index
                entry_index = i; // entry index of the FAT table
                if((total_fat_entries - i) >= entry_required){
                    // We can insert it into this position. No more allocation needed
                    break_loop = 1;
                    
                    int t_ = fblock.get_from_fat(traverse_cluster);
                    int t_2 = t_ >= END_CLUSTER;
                    break;
                    
                }
                else{
                        // We need to create an empty cluster.
                        // Write . and .. to the start 
                        //  after we have reached the end of the cluster
                        // we should move into the next one and start 
                        // reading from first entry.
                        new_cluster_index =  allocate_free_cluster(dblock,fblock); // a cluster for the directory
                        fblock.write_to_fat(traverse_cluster, new_cluster_index);
                        fblock.write_to_fat(new_cluster_index, END_CLUSTER);

                        break_loop = 1;
                        break;
                }
                free(cluster_pointer);
                return ;
            }
            else if(traverse_pointer->sequence_number == 0xE5){
                // Skip the erased content
                continue;
            }
            
            if(break_loop){
                break_loop = 0;
                break;
            }
        }
        if(break_loop){
            break_loop = 0;
            break;
        }
    }
    // new_cluster_index is the index of the new block (empty cluster)
    // dest_pointer points to the first empty directory entry
    

    // Open a cluster for our entry
    int dir_entry_cluster = allocate_free_cluster(dblock,fblock);
    fblock.write_to_fat(dir_entry_cluster,END_CLUSTER);

    // Creat the LFN and 8.3 entries
    // Fill the checksum, creation and modification date etc..
    vector<FatFileLFN> lfn_list(entry_required-1);
    FatFile83 f83_entry = create_entry(file_counter + 1,dir_entry_cluster,1);

    // fill the empty dir_entry_cluster
    // insert .  and .. entries

    void *subdirectory_ptr = dblock.get_from_dblock(dir_entry_cluster);
    FatFile83 *sub_ptr = (FatFile83 *) subdirectory_ptr;
    FatFile83 cur_entry = create_entry(-1, dir_entry_cluster,1); // point to current
    FatFile83 par_entry = create_entry(0, current_cluster,1); // point to parent

    *sub_ptr = cur_entry;
    *(sub_ptr + 1) = par_entry;

    subdirectory_ptr = (void *) sub_ptr;
    dblock.write_to_dblock(dir_entry_cluster,subdirectory_ptr);
    
    create_lfn_list(lfn_list,folder_name);  // fill out entries in lfn_list and f83_entry

    // Go to the index and update the pointers
    // Possible cases:
    // Case 1: new_cluster_index is -1 so our entries fit the cluster perfectly!
    // Case 2: new_cluster_index does exist.  Write to that cluster.
    // Case 3: new_cluster_index does exist. However newly allocated cluster is not enough as well. Allocate another one!
    
    // Case 1:
    int entry_left = entry_required;
    int entry_registered = 0;
    if(new_cluster_index == -1){
        void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
        for(int j = entry_index; j < total_fat_entries; j++){

            FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
            traverse_pointer += j;

            if(entry_left-- == 1){ // Thats the f83
                *traverse_pointer = *((FatFileLFN *) &f83_entry);
                break;
            }
            else{
                *traverse_pointer = lfn_list[entry_registered++];
            }

        }   
        dblock.write_to_dblock(traverse_cluster,cluster_pointer);
        void *tst = dblock.get_from_dblock(traverse_cluster);

        for(int j = 0; j < total_fat_entries; j++){
            FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
            traverse_pointer += j;
        }   

    }
    else{// Case 2 and 3
        while(entry_left > 0){
            void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
            for(int j = entry_index; j < total_fat_entries; j++){
                FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
                traverse_pointer += j;

                if(entry_left-- == 1){ // Thats the f83
                    *traverse_pointer = *((FatFileLFN *) &f83_entry);
                    break;
                }
                else{
                    *traverse_pointer = lfn_list[entry_registered++];
                }
            }

            dblock.write_to_dblock(traverse_cluster,cluster_pointer);

            if(entry_left > 0){
                if(fblock.get_from_fat(traverse_cluster) >= END_CLUSTER){  
                    new_cluster_index =  allocate_free_cluster(dblock,fblock); // a cluster for the directory
                    fblock.write_to_fat(traverse_cluster, new_cluster_index);
                    fblock.write_to_fat(new_cluster_index, END_CLUSTER);
                }
                traverse_cluster = fblock.get_from_fat(traverse_cluster);
                entry_index = 0;

            }

        
        }
    }

    // Now :
    // 1- We set up the entries. 
    // 2- Their attributes are correct
    // 3- For F83, the cluster allocated to this entry is also correct
    // All we need is to update the modification time of parent directory.
    string parent_destination;
    string parent_folder;
    seperate_path_file(parent_destination, parent_folder, starting_directory);
    //cout << "path is : " << path << endl;
    //cout << "Started at : " << starting_directory << endl;
    //cout << parent_destination << " | " << parent_folder << endl;
    cd_modify(starting_directory, starting_directory, starting_cluster, dblock, fblock);
    // cd .. -> update
}
 
 void touch(parsed_input *pinput, string starting_directory, int starting_cluster, DATA_Block &dblock, FAT_Block &fblock){
    // A function of mkdir. First traverse to the path 
    string current_directory = starting_directory;
    int current_cluster = starting_cluster;

    string temp_curr_directory = starting_directory;
    int temp_curr_cluster = starting_cluster;


    string temp2_curr_directory = starting_directory;
    int temp2_curr_cluster = starting_cluster;

    int total_fat_entries = dblock.get_cluster_size() / sizeof(FatFile83);

    string path;
    string folder_name;

    
    string arg1 = string(pinput->arg1);

    seperate_path_file(path,folder_name,arg1);


    // CD into directory if possible
    int path_exist = cd_(path,current_directory,current_cluster,dblock,fblock);

    if(path_exist == -1){
        return;
    }   

    int folder_exist = cd_(arg1,temp_curr_directory,temp_curr_cluster,dblock,fblock);

    if(folder_exist != -1){ // if folder exist
        return;
    }

    // So now we are at the current directory and current cluster. We have to create folder
    // Check whether there is a partial space in the cluster
    int entry_required   = folder_name.size()/13 + 2; // if less than 13 : 1 LFN + 1 83
    FatFileLFN* dest_pointer;
    // Traverse the cluster and the next clusters using FAT table
    int break_loop = 0;
    int directory_index = -1;
    int new_cluster_index = -1;
    int cluster_traveled = 0;
    int file_counter = 0;
    int traverse_cluster;
    int entry_index;
    for(traverse_cluster = current_cluster; traverse_cluster < END_CLUSTER; traverse_cluster = fblock.get_from_fat(traverse_cluster)){
        cluster_traveled++;
        void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
        vector<FatFileLFN*> lfn_vec;
        // LFN entries come first.
        int i = 0;
        file_counter = 0;
        for(; i < total_fat_entries; i++){    
            // If not the root, we have to start from 2, else start from 0
            FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
            traverse_pointer += i;
            int next_true_entry = (static_cast<int>(traverse_pointer->sequence_number) == 1) || (static_cast<int>(traverse_pointer->sequence_number) == 65);
            if(next_true_entry){
                file_counter++;
            }
            if(traverse_pointer->sequence_number == 0x00){
                // After this entry, all entries are availible. 
                dest_pointer = traverse_pointer ; // pointer to such index
                entry_index = i; // entry index of the FAT table
                if((total_fat_entries - i) >= entry_required){
                    // We can insert it into this position. No more allocation needed
                    break_loop = 1;
                    
                    int t_ = fblock.get_from_fat(traverse_cluster);
                    int t_2 = t_ >= END_CLUSTER;
                    break;
                    
                }
                else{
                        // We need to create an empty cluster.
                        // Write . and .. to the start 
                        //  after we have reached the end of the cluster
                        // we should move into the next one and start 
                        // reading from first entry.
                        new_cluster_index =  allocate_free_cluster(dblock,fblock); // a cluster for the directory
                        fblock.write_to_fat(traverse_cluster, new_cluster_index);
                        fblock.write_to_fat(new_cluster_index, END_CLUSTER);

                        break_loop = 1;
                        break;
                }
                free(cluster_pointer);
                return ;
            }
            else if(traverse_pointer->sequence_number == 0xE5){
                // Skip the erased content
                continue;
            }
            
            if(break_loop){
                break;
            }
        }
        if(break_loop){
            break;
        }
    }
    // new_cluster_index is the index of the new block (empty cluster)
    // dest_pointer points to the first empty directory entry
    
    // Creat the LFN and 8.3 entries
    // Fill the checksum, creation and modification date etc..
    vector<FatFileLFN> lfn_list(entry_required-1);
    FatFile83 f83_entry = create_entry(file_counter + 1,0,0);
    
    create_lfn_list(lfn_list,folder_name);  // fill out entries in lfn_list and f83_entry

    // Go to the index and update the pointers
    // Possible cases:
    // Case 1: new_cluster_index is -1 so our entries fit the cluster perfectly!
    // Case 2: new_cluster_index does exist.  Write to that cluster.
    // Case 3: new_cluster_index does exist. However newly allocated cluster is not enough as well. Allocate another one!
    
    // Case 1:
    int entry_left = entry_required;
    int entry_registered = 0;
    if(new_cluster_index == -1){
        void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
        for(int j = entry_index; j < total_fat_entries; j++){

            FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
            traverse_pointer += j;

            if(entry_left-- == 1){ // Thats the f83
                *traverse_pointer = *((FatFileLFN *) &f83_entry);
                break;
            }
            else{
                *traverse_pointer = lfn_list[entry_registered++];
            }

        }   
        dblock.write_to_dblock(traverse_cluster,cluster_pointer);
        void *tst = dblock.get_from_dblock(traverse_cluster);

        for(int j = 0; j < total_fat_entries; j++){
            FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
            traverse_pointer += j;
        }   

    }
    else{// Case 2 and 3
        while(entry_left > 0){
            void *cluster_pointer = dblock.get_from_dblock(traverse_cluster);
            for(int j = entry_index; j < total_fat_entries; j++){
                FatFileLFN* traverse_pointer = (FatFileLFN *) cluster_pointer;
                traverse_pointer += j;

                if(entry_left-- == 1){ // Thats the f83
                    *traverse_pointer = *((FatFileLFN *) &f83_entry);
                    break;
                }
                else{
                    *traverse_pointer = lfn_list[entry_registered++];
                }
            }

            dblock.write_to_dblock(traverse_cluster,cluster_pointer);

            if(entry_left > 0){
                if(fblock.get_from_fat(traverse_cluster) >= END_CLUSTER){  
                    new_cluster_index =  allocate_free_cluster(dblock,fblock); // a cluster for the directory
                    fblock.write_to_fat(traverse_cluster, new_cluster_index);
                    fblock.write_to_fat(new_cluster_index, END_CLUSTER);
                }
                traverse_cluster = fblock.get_from_fat(traverse_cluster);
                entry_index = 0;

            }

        }
    }
    // Now :
    // 1- We set up the entries. 
    // 2- Their attributes are correct
    // 3- For F83, the cluster allocated to this entry is also correct
    // All we need is to update the modification time of parent directory.
    cd_modify(starting_directory, starting_directory, starting_cluster, dblock, fblock);
    // cd .. -> update
}

void run_program(int &EXIT_STATUS_, DATA_Block &dblock, FAT_Block &fblock){
    // Run the loop.
    // YETER ARTIK ÖDEV YAPMAK İSTEYMİORUM
    int current_cluster = ROOT_DIRECTORY;
    string current_directory = "/"; // at the start, we are in root
    parsed_input* pinput = new parsed_input();
    int command_length;
    int QUIT_RECEIVED = 0;


    while(!QUIT_RECEIVED){
        string current_command;
        std::cout << current_directory << ">";

        getline(cin,current_command);
        command_length = current_command.size(); 
        char *string_c_str = new char[command_length + 1]; // possible memory leak, dont forget to delete
        string_to_c_str(current_command,string_c_str); 
        parse(pinput,string_c_str);
        int command_type = pinput->type;
        if(command_type == QUIT){
            QUIT_RECEIVED = 1;
            EXIT_STATUS_ = QUIT;
        }
        else if(command_type == CD){
            cd(pinput,current_directory,current_cluster,dblock,fblock);
        }
        else if(command_type == LS){
            ls(pinput,current_directory,current_cluster,dblock,fblock);
        }
        else if(command_type == CAT){
            cat(pinput,current_directory,current_cluster,dblock,fblock);
        }
        else if(command_type == MKDIR){
            mkdir(pinput,current_directory,current_cluster,dblock,fblock);
        }
        else if(command_type == TOUCH){
            touch(pinput,current_directory,current_cluster,dblock,fblock);
        }

        clean_input(pinput);
        delete[] string_c_str; // free allocated memory
    } 
}



int main(int argc, char *argv[])
{
    // Read image file
    BPB_struct bpb;
	int fd;
    string path_to_image = argv[1];
	fd = open(path_to_image.c_str(), O_RDWR);
	read(fd, &bpb, BPBS);

    FAT_Block fat_b = FAT_Block(bpb,fd);
    DATA_Block data_b = DATA_Block(bpb,fd);

    string current_directory = "/";
    int current_cluster = 2;

    int EXIT_STATUS;
    run_program(EXIT_STATUS, data_b, fat_b);
	parsed_input parsed_command;
    return 0;
}