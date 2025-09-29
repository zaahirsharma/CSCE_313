/*
    Original author of the starter code
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 2/8/20

    Name: Zaahir Sharma
    UIN: 134004682
    Date: 9/26/2025
*/

#include "common.h"
#include "FIFORequestChannel.h"
#include <sys/wait.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

int main(int argc, char* argv[]) {
    int option_char;
    int p = 1;
    double t = 0.0;
    int e = 1;
    int m = MAX_MESSAGE;
    bool new_channel_requested = false;
    bool time_flag = false;
    bool ecg_flag = false;
    string file_arg = "";

    while ((option_char = getopt(argc, argv, "p:t:e:f:m:c")) != -1) {
        switch (option_char) {
            case 'p':
                p = atoi(optarg); 
                break; 
            case 't': 
                t = atof(optarg); 
                time_flag = true; 
                break;
            case 'e': 
                e = atoi(optarg); 
                ecg_flag = true; 
                break;
            case 'f': 
                file_arg = optarg; 
                break;
            case 'm': 
                m = atoi(optarg); 
                break;
            case 'c': 
                new_channel_requested = true; 
                break;
        }
    }

    // Create received directory if it doesn't exist
    mkdir("received", 0777);

    pid_t child_pid = fork();
    if (child_pid == 0) {
        string m_str = to_string(m);
        char* server_args[] = {(char*)"./server", (char*)"-m", (char*)m_str.c_str(), nullptr};
        execv("./server", server_args);
        perror("execv failed");
        exit(1);
    } else if (child_pid < 0) {
        perror("fork failed");
        exit(1);
    }

    FIFORequestChannel* control_channel = nullptr;
    FIFORequestChannel* active_channel = nullptr;

    try {
        control_channel = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
        active_channel = control_channel;

        if (new_channel_requested) {
            MESSAGE_TYPE nc_msg = NEWCHANNEL_MSG;
            control_channel->cwrite(&nc_msg, sizeof(MESSAGE_TYPE));
            char new_chan_name[100];
            control_channel->cread(new_chan_name, sizeof(new_chan_name));
            active_channel = new FIFORequestChannel(new_chan_name, FIFORequestChannel::CLIENT_SIDE);
        }

        if (file_arg.empty()) {
            if (!time_flag && !ecg_flag) {
                ofstream out_csv("received/x1.csv");

                for (int i = 0; i < 1000; i++) {
                    double current_time = i * 0.004;

                    datamsg msg1(p, current_time, 1);
                    active_channel->cwrite(&msg1, sizeof(datamsg));
                    double ecg_val1; active_channel->cread(&ecg_val1, sizeof(double));

                    datamsg msg2(p, current_time, 2);
                    active_channel->cwrite(&msg2, sizeof(datamsg));
                    double ecg_val2; active_channel->cread(&ecg_val2, sizeof(double));

                    ostringstream oss_time;
                    oss_time << setprecision(15) << current_time;
                    string time_str = oss_time.str();
                    if (time_str.find('.') != string::npos) {
                        while (time_str.back() == '0') time_str.pop_back();
                        if (time_str.back() == '.') time_str.pop_back();
                    }

                    auto format_ecg = [](double val) {
                        ostringstream oss;
                        oss << fixed << setprecision(3) << val;
                        string s = oss.str();
                        while (s.back() == '0') s.pop_back();
                        if (s.back() == '.') s.pop_back();
                        return s;
                    };

                    out_csv << time_str << "," << format_ecg(ecg_val1) << "," << format_ecg(ecg_val2) << "\n";
                }

                out_csv.close();
            } else {
                datamsg single_req(p, t, e);
                active_channel->cwrite(&single_req, sizeof(datamsg));
                double reply_val; active_channel->cread(&reply_val, sizeof(double));
                cout << "For person " << p << ", at time " << t << ", the value of ecg " << e << " is " << reply_val << endl;
            }
        } else {
            vector<string> paths_to_try;
            string base_file = file_arg.substr(file_arg.find_last_of("/")+1);
            paths_to_try.push_back(base_file);
            paths_to_try.push_back("BIMDC/" + base_file);
            paths_to_try.push_back(file_arg);

            __int64_t fsize = -1;
            string selected_file;
            char* file_buf = new char[sizeof(filemsg) + file_arg.size() + 1];

            for (const auto& path : paths_to_try) {
                filemsg req(0,0);
                int len = sizeof(filemsg) + path.size() + 1;
                memcpy(file_buf, &req, sizeof(filemsg));
                strcpy(file_buf + sizeof(filemsg), path.c_str());
                active_channel->cwrite(file_buf, len);
                int bytes_read = active_channel->cread(&fsize, sizeof(__int64_t));
                if (bytes_read == sizeof(__int64_t) && fsize >= 0) {
                    selected_file = path;
                    break;
                }
            }

            if (fsize < 0) {
                cerr << "Error: file not found\n"; delete[] file_buf; exit(1);
            }

            FILE* out_file = fopen(("received/" + base_file).c_str(), "wb");
            char* transfer_buf = new char[m];
            __int64_t transferred = 0;

            while (transferred < fsize) {
                __int64_t to_request = min((__int64_t)m, fsize - transferred);
                filemsg chunk_req(transferred, (int)to_request);
                int len = sizeof(filemsg) + selected_file.size() + 1;
                memcpy(transfer_buf, &chunk_req, sizeof(filemsg));
                strcpy(transfer_buf + sizeof(filemsg), selected_file.c_str());
                active_channel->cwrite(transfer_buf, len);
                int received = active_channel->cread(transfer_buf, to_request);
                fwrite(transfer_buf, 1, received, out_file);
                transferred += received;
            }

            fclose(out_file);
            delete[] file_buf; 
            delete[] transfer_buf;
        }

        MESSAGE_TYPE quit_msg = QUIT_MSG;
        if (new_channel_requested && active_channel != control_channel) {
            active_channel->cwrite(&quit_msg, sizeof(MESSAGE_TYPE));
            delete active_channel;
        }
        control_channel->cwrite(&quit_msg, sizeof(MESSAGE_TYPE));
        delete control_channel;

    } catch (const exception& ex) {
        cerr << "Exception: " << ex.what() << endl;
        if (control_channel) delete control_channel;
        if (new_channel_requested && active_channel && active_channel != control_channel) delete active_channel;
        exit(1);
    }

    wait(nullptr);
    return 0;
}
