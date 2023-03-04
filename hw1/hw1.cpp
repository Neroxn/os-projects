#include "parser.c"
#include <syscall.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <map>
#include<fcntl.h> 
#include<errno.h> 
#include <sys/stat.h>
#include <sys/wait.h>

using namespace std;


/*
Bundle without Redirection:
    - Fork necessary amount of children.
    - Execute each command with execvp


*/

#define MAX_BUFFER 256


void test_pipe(int pipe_fd){
    // Test pipe by reading its content and writing it to pipe-content.txt
    char buf[256]; // User defined for testing
    int n = read(pipe_fd,&buf,256);
    fprintf(stderr, "%d bytes readed : %s",n,buf);
}

void execute_bundle(vector<parsed_input *> bundle, string input, string output){
    // Execute the bundle.
    // Fork bundle size many process and for each

    pid_t *pid_array = (pid_t *) malloc(bundle.size() - 1); // how many child we should have    
    ofstream file_out;
    int fdout;

    // If bundle has an input/output redirection, handle it using dup2
    if(output != ""){
        const char *output_ = output.c_str();
        fdout = open(output_,  O_CREAT | O_APPEND | O_WRONLY , 0666); 
        //chmod(output_,S_IRWXU);
    }

    int recvsignal;
    for(int i =1; i < bundle.size(); i++){
        pid_array[i] = fork();
        if(pid_array[i]){
            ;
        }

        else{
            if(output != ""){
                dup2(fdout,1); // only affect the childrens output
                close(fdout);
            }

            if(input != ""){ // For each child, open new file to read the whole file.
                int fdin;
                const char *input_ = input.c_str();
                fdin = open(input_, O_RDONLY); 
                dup2(fdin,0);
                close(fdin);
            }
            execvp((bundle[i])->argv[0], (bundle[i])->argv); // After this execution input/output redirection is satisfied
        }
    }

    for(int i = 1; i < bundle.size(); i++){
        wait(&recvsignal);
    }
}

void execute_bundle_(vector<parsed_input *> bundle, int input, int output, string a_input, string a_output){
    // Execute the bundle.
    // Fork bundle size many process and for each

    pid_t *pid_array = (pid_t *) malloc(bundle.size() - 1); // how many child we should have
    ofstream file_out;
    int recvsignal;
    int fdin,fdout;

    if(a_output != ""){
        const char *output_ = a_output.c_str();
        fdout = open(output_, O_CREAT | O_APPEND  | O_WRONLY , 0666); 
       // chmod(output_,S_IRWXU);
    }

    for(int i =1; i < bundle.size(); i++){
        pid_array[i] = fork();
        if(pid_array[i]){;}
        else{

            // If bundle has an input/output redirection, handle it using dup2
            if(a_input != ""){ // For each child, open new file to read the whole file.
                int fdin;
                const char *input_ = a_input.c_str();
                fdin = open(input_, O_RDONLY); 
                dup2(fdin,0);
                close(fdin);
            }

            else{
                if(input != 0){ // For each child, open new file to read the whole file.
                    dup2(input,0);
                    close(input);
                }
            }

            if(a_output == ""){
                if(output != 1){
                    dup2(output,1);
                    close(output);
                }
            }

            execvp((bundle[i])->argv[0], (bundle[i])->argv); // After this execution input/output redirection is satisfied
        }
    }
}

void execute_process(parsed_input *pinput, int input, int output, string a_input, string a_output){


    int fdout,fdin;
    if(a_output != ""){
        const char *output_ = a_output.c_str();
        fdout = open(output_, O_CREAT  | O_APPEND  | O_WRONLY, 0666);

        //chmod(output_,S_IRWXU);
        dup2(fdout,1); // only affect the childrens output
        close(fdout);
    }
    else{
        if(output != 1){

            dup2(output,1);
            close(output);
        }
    }


    if(a_input != ""){ // For each child, open new file to read the whole file.
        int fdin;
        const char *input_ = a_input.c_str();
        fdin = open(input_, O_RDONLY); 
        dup2(fdin,0);
        close(fdin);
    }
    else{
        if(input != 0){ // For each child, open new file to read the whole file.
            dup2(input,0);
            close(input);
        }
    }

    execvp(pinput->argv[0],pinput->argv);
    
}

void read_all_pipe(int fd, string &content, int previous_bundle){
    char ch;
    int recvsignal;

    while(read(fd, &ch,1) > 0){ // read the whole pipe!
        content.push_back(ch);
    }

    close(fd);

    for(int i = 1; i < previous_bundle; i++){
        wait(NULL); // wait previous executions to finish - reap processors

    }
}

void execute(vector<parsed_input *> bundle, int is_last_bundle, int *pipe_fd_vec, const char *buffer, string output){
    
    
    int fd_repeat[2];
    string empty = "";
    int bundle_size = bundle.size();
    for(int i = 1; i < bundle_size; i++){

        pipe(fd_repeat);
        int fd = fork();
        if(fd == 0){
            close(fd_repeat[1]);


            if(is_last_bundle){
                execute_process(bundle[i],fd_repeat[0],1,empty,output);
            }
            else{
                execute_process(bundle[i],fd_repeat[0],pipe_fd_vec[1],empty,empty);
            }
        }   
        else{
            close(fd_repeat[0]);
            write(fd_repeat[1],buffer,strlen(buffer));
            close(fd_repeat[1]); // we dont need this
        }
    }



}
void piped_execution_(parsed_input *pinput, map<string, vector<parsed_input *>> &bundleMap)
{
    string empty = "";
    string output = "";
    string input_fb = "";
    int n_many_bundles = pinput->command.bundle_count;
    int debug = 0;

    int pdf[2]; // current pipe
    pipe(pdf);

    // Set up required pipe for sequential implementation
    
    // Get first bundle to start execution
    string first_bundle = string(pinput->command.bundles[0].name);
    vector<parsed_input *>fb = bundleMap[first_bundle];



    int previous_bundle = fb.size();
    if(pinput->command.bundles[0].input){
        input_fb = string(pinput->command.bundles[0].input);
    }

    if(debug)       {cout << "Passed - 0 - " << input_fb << endl;}
    execute_bundle_(fb,1,pdf[1],input_fb,empty);
    if(debug)       {cout << "Passed - 1 " <<  n_many_bundles << endl;}

    close(pdf[1]); // Since first redirection occured, we can close it safely

    int pdf_[2]; // next bundle's pipe if necessary
    pipe(pdf_);

    // loop through all bundles and sequentially, update the pipes
    for(int i = 1; i < n_many_bundles; i++){
        string content;
        if(debug)       {cout << "Passed - will read - " << endl;}

        read_all_pipe(pdf[0],content,previous_bundle);
        const char *string_content = content.c_str();
        if(debug)       {cout << "Passed - 2 - " <<  strlen(string_content) << endl;}


        if(debug)       {cout << "Passed - 3 - " << endl;}
        string nxt_bundle = string(pinput->command.bundles[i].name);  // represents the next bundle
        vector<parsed_input *>nb = bundleMap[nxt_bundle];

        previous_bundle = nb.size();
        int is_last_bundle = (i == n_many_bundles - 1);

        if(debug)       {cout << "Passed - 4 - "  << is_last_bundle << endl;}

        if(is_last_bundle){
            if(pinput->command.bundles[i].output){
                output = string(pinput->command.bundles[i].output);
            }
        }
        execute(nb,is_last_bundle,pdf_,string_content,output);

        if(debug)       {cout << "Passed - 5 - " << output << endl;}

        // swap pipes  pipe_fd1 to pipe_fd2 and close  old pipes
        pdf[0] = pdf_[0]; 
        pdf[1] = pdf_[1];

        close(pdf[1]);
        pipe(pdf_);

        if(debug)       {cout << "Passed - 6 - " << endl;}
    }

    for(int i = 1; i < previous_bundle; i++){
        wait(NULL);
    }

}


int main(){

    char inp[MAX_BUFFER];
    map<string, vector<parsed_input *>> bundleMap; 
    //vector<vector<parsed_input *>> bundles = create_bundles(inp,bundleMap); // input now holds the bundle usage line.
    
    int bundle_creation = 0;
    int parse_return;
    string bundle_name;
    while(1){
        fgets(inp, MAX_BUFFER, stdin);
        parsed_input *pinput = (parsed_input *) malloc(sizeof(parsed_input));
        parse(inp,bundle_creation,pinput);

        if(pinput->command.type == PROCESS_BUNDLE_CREATE){ // if the comamnd is pbc

            vector<parsed_input*> bundle;
            bundle_creation = 1;
            bundle_name = string(pinput->command.bundle_name);

            bundle.push_back(pinput);


            while(fgets(inp, MAX_BUFFER, stdin)){ // Bundle creation loop
                parsed_input *pinput = (parsed_input *) malloc(sizeof(parsed_input));

                parse_return = parse(inp,bundle_creation,pinput);

                
                // pinput is holding information about the line
                if(parse_return == 1){
                    bundle_creation = 0;
                    bundleMap[bundle_name] = bundle;
                    break; // break loop if psb encountered
                }

                bundle.push_back(pinput);
            }
        }

        else if(pinput->command.type == QUIT){
            break; // break from the loop
        }

        else if(pinput->command.type == PROCESS_BUNDLE_EXECUTION){ // execution stage
            if(pinput->command.bundle_count > 1){
                //cout << "Testing piped execution." << endl; // endl
                piped_execution_(pinput,bundleMap);
            }
            else{            
                string bundle_name = "";
                string incoming_input_from = "";
                string redirected_output_to = "";

                if(pinput->command.bundles[0].name){
                    //cout << pinput->command.bundles[0].name << endl; 
                    bundle_name = string(pinput->command.bundles[0].name);
                }
                if(pinput->command.bundles[0].input){
                    //cout << pinput->command.bundles[0].input << endl;
                    incoming_input_from =  string(pinput->command.bundles[0].input);
                }
                if(pinput->command.bundles[0].output){
                    //cout << pinput->command.bundles[0].output << endl;
                    redirected_output_to = string(pinput->command.bundles[0].output);
                }
                execute_bundle(bundleMap[bundle_name],incoming_input_from,redirected_output_to);
            }
        }
        

    }
    return 0;
}
