#Purpose
plda is for modeling text and inferring relationships between documents by LDA algorithm.
However, it does not re-utilize the model database for inference, which results in loading the large database everytime.  
plda-server, this forked project, acts as an server for plda inference processing and keeps running with that database loaded.  
Here is also client example code in python.

#Version
This is actually forked from plda-3.1.
https://code.google.com/p/plda/downloads/list

#Requirement
This uses pthread and socket.
You need a model file (see the above URL about plda basics) in server side and input file with terms and frequencies (bag-of-words) in client side.

SSL/TLS is supported. You need to install OpenSSL.

#Usage example
./infer --alpha 0.1 --beta 0.01 --model_file enwiki-model.txt --burn_in_iterations 100 --total_iterations 150 --src_sock_port 5328 --daemonize --verbose  

python sock_client.py -f enwiki-input.txt -i 127.0.0.1 -p 5328 -v

