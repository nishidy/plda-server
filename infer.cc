// Copyright 2008 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/*
  An example running of this program:

  ./infer \
  --alpha 0.1    \
  --beta 0.01                                           \
  --inference_data_file ./testdata/test_data.txt \
  --inference_result_file /tmp/inference_result.txt \
  --model_file /tmp/lda_model.txt                       \
  --burn_in_iterations 10                              \
  --total_iterations 15
*/
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "common.h"
#include "document.h"
#include "model.h"
#include "sampler.h"
#include "cmd_flags.h"

#include <iostream>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // inet_ntoa
#include <sys/wait.h> // waitpid
#include <string.h> // bzero()
#include <sys/syscall.h> // syscall()
#include <sys/time.h> // usleep()
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h> //setsid etc.

#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define LBUF  65535

using learning_lda::LDACorpus;
using learning_lda::LDAModel;
using learning_lda::LDAAccumulativeModel;
using learning_lda::LDASampler;
using learning_lda::LDADocument;
using learning_lda::LDACmdLineFlags;
using learning_lda::DocumentWordTopicsPB;
using learning_lda::RandInt;
using std::ifstream;
using std::ofstream;
using std::istringstream;

using namespace std;

// signal
int g_srcSocket;

/*
void Wait(int sig){
  while(waitpid(-1,NULL,WNOHANG)<0);
  signal(SIGCHLD, Wait);
}
*/

void Finish(int sig){
    cout << "SIGTERM: Close the socket(" << g_srcSocket << ")" << endl;
    close(g_srcSocket);
}

pid_t gettid(void)
{
    return syscall(SYS_gettid);
}

struct inf_data {
    int sock;
	SSL *ssl;
	string client_ip;
    string res;
    LDACmdLineFlags *flags;
    map<string, int> *word_index_map;
    LDAModel *model;
    LDASampler *sampler;
};

void* infer(void* p){

    struct inf_data* infdata = (struct inf_data*)p;
    int dstSocket  = infdata->sock;
    SSL *ssl = infdata->ssl;
	string client_ip = infdata->client_ip;

    map<string, int> *word_index_map = infdata->word_index_map;
    LDAModel *model = infdata->model;
    LDACmdLineFlags *flags = infdata->flags;

    int width;
    fd_set mask;

    FD_ZERO(&mask);
    FD_SET(dstSocket, &mask);
    width = dstSocket+1;
 
    pid_t tid=gettid();
    if(flags->verbose_)
		cout << "Thread id : " << tid << endl;


    int n=0,numrcv=0;
    char buf[LBUF] = {'\0'};
    string line;
    fd_set readok;

    for(;;){

        line.clear();

        for(;;){
 
            memcpy(&readok,&mask,sizeof(fd_set));

	        n = select(width, &readok, NULL, NULL, NULL);

	        if( n==-1 ) {
				cerr << "@select: Error " << errno <<
				        " in a thread(" << tid << ")." << endl;
         	    shutdown(dstSocket,SHUT_RDWR);
                break;
            }else if( n==0 ){
				cerr << "@select: Timeout " << errno <<
				        " in a thread(" << tid << ")." << endl;
                shutdown(dstSocket,SHUT_RDWR);
                break;
            }
  
            if(!FD_ISSET(dstSocket,&readok))
                continue;
  
            memset(buf,'\0',LBUF);
  			if(ssl==NULL){
				numrcv = read(dstSocket, buf, LBUF);
			}else{
	 			if(SSL_accept(ssl) <= 0){
					ERR_print_errors_fp(stderr);
					break;
				}else{
					numrcv = SSL_read(ssl, buf, sizeof(buf));
				}
			}

            if( numrcv==-1 ){ // socket shutdowned
                cout << "@read: Socket shutdowned(" << dstSocket << ")." << endl;
                close(dstSocket);
                break;
            }else if( numrcv==0 ){ // socket closed
                cout << "@read: Socket was closed(" << dstSocket << ")." << endl;
                close(dstSocket);
                break;
			}

            line += (string)buf;
			// The end of data is '\n'
            if(line.size()>0 && line.at(line.size()-1)=='\n'){
                break;
            }

        } // select & read loop ends

        if( n==0 || n==-1 ) break; // select error/timeout
        if( numrcv==0 || numrcv==-1 ) break; // socket closed/shutdowned
  

        LDASampler sampler(flags->alpha_, flags->beta_, model, NULL);
  
        if (line.size() > 0 &&      // Skip empty lines.
            line[0] != '\r' &&      // Skip empty lines.
            line[0] != '\n' &&      // Skip empty lines.
            line[0] != '#') {       // Skip comment lines.

            istringstream ss(line);
            DocumentWordTopicsPB document_topics;
            string word;
            int count,term_count=0;

            while (ss >> word >> count) {  // Load and init a document.
                vector<int32> topics;
                for (int i = 0; i < count; ++i) {
                    topics.push_back(RandInt(model->num_topics()));
                }

                map<string, int>::const_iterator iter = word_index_map->find(word);
                if (iter != word_index_map->end()) {
                    document_topics.add_wordtopics(word, iter->second, topics);
                }
				term_count++;
            }

    		if(flags->verbose_){
				cout << "Received from client[" << client_ip << "] (# of terms is ";
				cout << term_count << "): " << line << endl;
			}


            LDADocument document(document_topics, model->num_topics());
            TopicProbDistribution prob_dist(model->num_topics(), 0);

            for (int iter = 0; iter < flags->total_iterations_; ++iter) {
                sampler.SampleNewTopicsForDocument(&document, false);
                if (iter >= flags->burn_in_iterations_) {
                    const vector<int64>& document_distribution =
                    document.topic_distribution();
                    for (int i = 0; i < document_distribution.size(); ++i) {
                        prob_dist[i] += document_distribution[i];
                    }
                }
            }

            double d;
            string out="";
            for (int topic = 0; topic < prob_dist.size(); ++topic) {
                char tmp[16] = {'\0'};

				//Original code
                //out << prob_dist[topic] /
                //      (flags.total_iterations_ - flags.burn_in_iterations_)
                //    << ((topic < prob_dist.size() - 1) ? " " : "\n");
  
                d = prob_dist[topic] /\
                    (flags->total_iterations_ - flags->burn_in_iterations_);

                sprintf(tmp,"%.2f",d);
                out += (string)tmp;
                out += ((topic < prob_dist.size() - 1) ? " " : "\n");
            }
      
	  		if(ssl==NULL){
	            write(dstSocket, out.c_str(), out.size());
			}else{
            	SSL_write(ssl, out.c_str(), out.size());
			}

			break;

        } // if

    } // for(;;)

	close(dstSocket);
	/*
	if(ssl!=NULL){
		SSL_free(ssl);
	}
	*/

    return 0;
}

SSL* setup_ssl(string cert_file, string key_file){

  SSL_load_error_strings();
  SSL_library_init();

  SSL_CTX *ctx = SSL_CTX_new(SSLv3_server_method());
  if(ctx == NULL){
	  ERR_print_errors_fp(stderr);
	  return NULL;
  }

  // Load certificate
  if( SSL_CTX_use_certificate_file(ctx,cert_file.c_str(),SSL_FILETYPE_PEM) <=0 ){
	  ERR_print_errors_fp(stderr);
	  return NULL;
  }

  if( SSL_CTX_use_PrivateKey_file(ctx,key_file.c_str(),SSL_FILETYPE_PEM) <=0 ){
	  ERR_print_errors_fp(stderr);
	  return NULL;
  }

  if( !SSL_CTX_check_private_key(ctx) ){
	  fprintf(stderr, "Private key does not match the public certificate\n");
	  return NULL;
  }

  return SSL_new(ctx);

}

int main(int argc, char** argv) {

  LDACmdLineFlags flags;
  flags.ParseCmdFlags(argc, argv);
  if (!flags.CheckInferringValidity()) {
    return -1;
  }
  srand(time(NULL));

  map<string, int> word_index_map;
  ifstream model_fin(flags.model_file_.c_str());
  LDAModel model(model_fin, &word_index_map);

  struct sockaddr_in srcAddr;
  bzero((char*)&srcAddr, sizeof(srcAddr));

  srcAddr.sin_port = htons(flags.src_sock_port_);
  srcAddr.sin_family = AF_INET;
  srcAddr.sin_addr.s_addr = INADDR_ANY;

  int srcSocket = socket(AF_INET, SOCK_STREAM, 0);
  g_srcSocket = srcSocket;

  SSL* ssl=NULL;
  if(!flags.cert_file_.empty()&&!flags.key_file_.empty()){
	  ssl = setup_ssl(flags.cert_file_,flags.key_file_);
	  if(ssl==NULL) exit(99);
  }

  signal(SIGTERM, Finish);

  int temp = 1;

  // Next time, TIME_WAIT may cause the failure of bind
  // SO_REUSEADDR option can avoid this issue
  if(setsockopt(srcSocket, SOL_SOCKET, SO_REUSEADDR, &temp, sizeof(temp)))
      fprintf(stderr, "setsockopt() failed");

  if(bind(srcSocket, (struct sockaddr *)&srcAddr, sizeof(srcAddr))!=0){
      printf("bind failed %d\n",errno);
      return -1;
  }

  if(listen(srcSocket, 100)!=0){
      printf("listen failed %d\n",errno);
      return -1;
  }

  if(flags.daemonize_){
      pid_t pid;

      // parent should exit to make child daemonize
      if((pid=fork())>0) exit(1);

      // Create new session without terminal control
      pid=setsid();
  }

  int dstSocket;
  struct sockaddr_in dstAddr;
  int dstAddrSize = sizeof(dstAddr);

  for(;;){

    dstSocket = accept(srcSocket, (struct sockaddr *)&dstAddr, (socklen_t *)&dstAddrSize);
    if( dstSocket <= 0 ){
        cerr << "@accept: Socket was closed(" << srcSocket << ")." << endl;
        break;
    }

	if(ssl!=NULL)
		SSL_set_fd(ssl, dstSocket);

    {
        struct inf_data infdata;

        infdata.sock = dstSocket;
        infdata.ssl = ssl;
		infdata.client_ip = inet_ntoa(dstAddr.sin_addr);
        infdata.word_index_map = &word_index_map;
        infdata.model = &model;
        infdata.flags = &flags;

        pthread_t pt;
        pthread_create(&pt,NULL,infer,(void *)&infdata);
        pthread_detach(pt);
    }

  }

  return 0;
}

