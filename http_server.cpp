#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <cstdlib>
#include <map>
#include <iostream>
#include <csignal>
#include <wait.h>
#include <netinet/in.h>
#include <ev.h>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sstream>
#include <fstream>

struct globalArgs_t { 
	char ip[INET_ADDRSTRLEN];
	int port;
	char path[1024];
} globalArgs;

struct my_io {
    struct ev_io watcher;
    int sock;
    std::map<pid_t, int>* pworkers;
};

struct my_child {
    struct ev_child watcher;
    std::map<pid_t, int>* pworkers;
};

struct my_signal {
    struct ev_signal watcher;
    std::map<pid_t, int>* pworkers;
};

int set_nonblock(int fd)
{
    int flags;
#if defined(O_NONBLOCK)
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
} 

ssize_t sock_fd_write(int sock, void *buf, ssize_t buflen, int fd)
{
    ssize_t     size;
    struct msghdr   msg;
    struct iovec    iov;
    union {
        struct cmsghdr  cmsghdr;
        char        control[CMSG_SPACE(sizeof (int))];
    } cmsgu;
    struct cmsghdr  *cmsg;

    iov.iov_base = buf;
    iov.iov_len = buflen;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    if (fd != -1) {
        msg.msg_control = cmsgu.control;
        msg.msg_controllen = sizeof(cmsgu.control);

        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_len = CMSG_LEN(sizeof (int));
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;

        // printf ("passing fd %d\n", fd);
        *((int *) CMSG_DATA(cmsg)) = fd;
    } else {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
        // printf ("not passing fd\n");
    }

    size = sendmsg(sock, &msg, 0);

    if (size < 0)
        perror ("sendmsg");
    return size;
}

ssize_t sock_fd_read(int sock, void *buf, ssize_t bufsize, int *fd)
{
    ssize_t     size;

    if (fd) {
        struct msghdr   msg;
        struct iovec    iov;
        union {
            struct cmsghdr  cmsghdr;
            char        control[CMSG_SPACE(sizeof (int))];
        } cmsgu;
        struct cmsghdr  *cmsg;

        iov.iov_base = buf;
        iov.iov_len = bufsize;

        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cmsgu.control;
        msg.msg_controllen = sizeof(cmsgu.control);
        size = recvmsg (sock, &msg, 0);
        if (size < 0) {
            perror ("recvmsg");
            exit(EXIT_FAILURE);
        }
        cmsg = CMSG_FIRSTHDR(&msg);
        if (cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
            if (cmsg->cmsg_level != SOL_SOCKET) {
                fprintf (stderr, "invalid cmsg_level %d\n",
                     cmsg->cmsg_level);
                exit(EXIT_FAILURE);
            }
            if (cmsg->cmsg_type != SCM_RIGHTS) {
                fprintf (stderr, "invalid cmsg_type %d\n",
                     cmsg->cmsg_type);
                exit(EXIT_FAILURE);
            }

            *fd = *((int *) CMSG_DATA(cmsg));
            // printf ("received fd %d\n", *fd);
        } else
            *fd = -1;
    } else {
        size = read (sock, buf, bufsize);
        if (size < 0) {
            perror("read");
            exit(EXIT_FAILURE);
        }
    }
    return size;
}



void childprocess(int socket) {

    int fd;
    char buf[16];
    ssize_t size;
        // std::cerr << "Процесс " << getpid() << " запустился, сокет = " << socket << std::endl << std::flush;
    for(;;) {
        size = sock_fd_read(socket, buf, sizeof(buf), &fd);
        if (size <= 0)
            break;
        if (fd != -1) {
        	// обработка запроса
        	// читаем из сокета HTTP запрос
		    char buffer[1024];
		    size_t r = recv(fd, buffer, 1024, MSG_NOSIGNAL);
		    if (r < 0) 
		        break;
		    else if (r != 0) {
		        // parse HTTP
		    	std::string str(buffer);
				std::stringstream ss(str);
				std::string temp;
				std::getline(ss, temp);
				size_t pos = temp.find(" ",0);
				size_t pos2 = temp.find(" ", pos + 1);
				size_t pos3 = temp.find("?", pos + 1);
				std::string command = temp.substr(0, pos);
				// std::cout << "command = " << command << std::endl << std::flush;
				if (command.compare("GET") == 0){ // пришла команда GET
					std::string content;
					if (pos3 < pos2){
						content = temp.substr(pos + 2, pos3 - pos - 2);
					} else {
						content = temp.substr(pos + 2, pos2 - pos - 2);
					}
					// std::cout << "content = " << content << std::endl << std::flush;
					
					// std::cout << "current_dir = " << get_current_dir_name() << std::endl << std::flush;

					int file = open(content.c_str(), O_RDONLY, 0666);
					std::stringstream out;
					if (file == -1) { // Not Found
						out << "HTTP/1.0 404 NOT FOUND";
						out << "\r\n";
						out << "Context-length: ";
						out << 0;
						out << "\r\n";
						out << "Context-type: text/html";
						out << "\r\n\r\n";
					} else { // OK
						close(file);
						std::ifstream f(content.c_str());
						std::stringstream tmp;
						tmp << f.rdbuf();
						std::string data(tmp.str());
						f.close();
						out << "HTTP/1.0 200 OK";
						out << "\r\n";
						out << "Content-length: ";
						out << data.size();
						out << "\r\n";
						out << "Content-Type: text/html";
						out << "\r\n\r\n";
						out << data;
					}
			        send(fd, out.str().c_str(), out.str().length(), MSG_NOSIGNAL);
				}
		    }
            // std::cerr << "Worker (childprocess):процесс " << getpid() << " принял дескриптор " << fd << std::endl<< std::flush;
            
            // закрытие соединения
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
    }

}

void create_child(std::map<pid_t, int> & workers) {
    int sv[2];
    int err = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    if (err == -1) {
        perror ("Socketpair failed. Can't create child");
        return;
    }
    int pid = fork();
    if (pid == -1) {
        perror("Fork failed!");
        exit(1);
    } else if (pid != 0) { // parent
        close(sv[1]);
        workers.emplace(std::make_pair(pid, sv[0]));
    } else if (pid == 0){ // child
        close(sv[0]);
        childprocess(sv[1]);
    }
}

void print_all_workers(std::map<pid_t, int> & workers) {
    std::cout << "----------" << workers.size() << "----------"<< std::endl;
    for (auto it = workers.cbegin(); it != workers.cend(); it++){
        std::cout << "pid = " << it->first << "; sv = " << it->second << std::endl;
    }
    std::cout << "--------------------"<< std::endl<< std::flush;
}

void close_all_socketpair_and_kill_child(std::map<pid_t, int> & workers){
    for (auto it = workers.cbegin(); it != workers.cend(); ++it){
        close(it->second);
        kill(it->first, SIGTERM);
        int status;
        waitpid(it->first, &status, 0);
    }
}

static void signals_handler(struct ev_loop *loop, ev_signal *watcher, int revents) { // действия при смерти worker
    struct my_signal *w = (struct my_signal*) watcher;
	ev_break(loop, EVBREAK_ALL);
	close_all_socketpair_and_kill_child(*(w->pworkers));
	exit(EXIT_FAILURE);
}


static void sigchld_handler(struct ev_loop *loop, ev_child *watcher, int revents) { // действия при смерти worker
    struct my_child *w = (struct my_child*) watcher;

    int pid = w->watcher.rpid;

    if (pid < 0) {
        perror("Error on waitpid");
    } else {
        //закрываем socketpair
        close(w->pworkers->at(pid));
        // erase from map
        w->pworkers->erase(w->pworkers->find(pid));
        // std::cout << "Master Process (sigchld_handler): pid = " << pid << " deleted " << std::endl;
        // std::cout << std::flush;
        create_child(*(w->pworkers)); // создаем нового worker
        print_all_workers(*(w->pworkers));
    }
}



void accept_cb(struct ev_loop *loop, struct ev_io * watcher, int revents) {
    struct my_io *w = (struct my_io*) watcher;
    
    static int current_worker = 0;

    int client_sd = accept(w->watcher.fd, 0, 0);
    if (client_sd <= 0) {
        return;
    }

    auto it = w->pworkers->cbegin();
    for (int i = 0; (i < current_worker) && (it!= w->pworkers->cend()); ++i)
        ++it;

    sock_fd_write(it->second, (void*) "1", 1, client_sd);
    close(client_sd);

//test
    // std::cout << "current_worker = " << current_worker << std::endl << std::flush;
    // print_all_workers(*(w->pworkers)); // test
    // std::cout << "first = " << it->first << ", second = " << it->second << std::endl << std::flush;
    // std::cerr << "Master Process (accept_cb): процессу " << it->first 
    //           << " отправил fd " << client_sd << " по sv " << it->second
    //           << "; current_worker = " << current_worker << std::endl << std::flush;
//test end    
    
    current_worker = (current_worker + 1) % w->pworkers->size();
}

int main(int argc, char **argv)
{
	// -h <ip> -p <port> -d <directory>
    // распарсить строку
	globalArgs.port = 12345;
	strcpy(globalArgs.ip, "127.0.0.1");
	strcpy(globalArgs.path, ".");
	int rez=0;

	while ( (rez = getopt(argc,argv,"h:p:d:")) != -1){
		switch (rez){
		case 'h':
			strcpy(globalArgs.ip, optarg);
			break;
		case 'p': 
			globalArgs.port = atoi (optarg);
			break;
		case 'd': 
			strcpy(globalArgs.path, optarg);
			break;
		default: break;
        };
	};
	// std::cout << "ip = " << globalArgs.ip << "; port = " << globalArgs.port << "; dir = " << globalArgs.path << std::endl << std::flush;

    // проверка правильности заполнения аргументов
    
    int dst;
    int res = inet_pton(AF_INET, globalArgs.ip, &dst);

    // std:: cout << "res = " << res << " addr.sin_addr = " << addr.sin_addr.s_addr << std::endl << std::flush;
    if (res == 0) {
    	std::cerr << "Error! Bad ip-address!" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (res < 0) {
        perror("Error! Incorrect ip-address!");
        exit(EXIT_FAILURE);
    }
    if (globalArgs.port < 0 || globalArgs.port > 65536) {
    	std::cerr << "Error! Bad port!" << std::endl;
        exit(EXIT_FAILURE);
    }

     // создаем демона
	pid_t pid, sid;
    pid = fork();
    if (pid < 0) {
    	perror("Fork canceled");
        exit(EXIT_FAILURE); // ошибка 
    }
    if (pid > 0) { //parent
            exit(EXIT_SUCCESS);
    }
	umask(0); // права по умолчанию - полный доступ
	sid = setsid();
	if (sid < 0) {
		perror("Setsid canceled");
		exit(EXIT_FAILURE);
	}


	if ((chdir(globalArgs.path)) < 0) {
		std::cerr << "Error! Directory doesn't exists!" << std::endl;
        exit(EXIT_FAILURE);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

    // MASTER PROCESS

    // создаем сокет
    int master_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);//tcp
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;

    addr.sin_port = htons(globalArgs.port);
    inet_pton(AF_INET, globalArgs.ip, &addr.sin_addr);

    // std:: cout << "res = " << res << " addr.sin_addr = " << addr.sin_addr.s_addr << std::endl << std::flush;
   
    bind(master_socket, (struct sockaddr *) &addr, sizeof(addr));
    set_nonblock(master_socket);
    int enable = 1;
    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");

    listen (master_socket, SOMAXCONN);

    std::map<pid_t, int> workers;
    // создаем дочерние процессы
    const int WORKERS_COUNT = 5; // количество worker'ов
    for (int i = 0; i < WORKERS_COUNT; ++i) { // создаем worker'ов
        create_child(workers);
        usleep(10000);
    }

    // создаем loop в master process
    struct ev_loop *loop_master = ev_default_loop(0);
    if (!loop_master) {
        perror("Error! Can't create default loop!");
        close_all_socketpair_and_kill_child(workers);
        exit(1);
    }
//test
    // print_all_workers(workers);
//test end
    // watcher на прием соединений
    struct my_io my_w_accept;
    my_w_accept.sock = 0;
    my_w_accept.pworkers = &workers;
    ev_io_init(&my_w_accept.watcher, accept_cb, master_socket, EV_READ);
    ev_io_start(loop_master, &my_w_accept.watcher);

    // watcher на восстановление worker'ов
    struct my_child my_w_child;
    my_w_child.pworkers = &workers;
    ev_child_init (&my_w_child.watcher, sigchld_handler, 0, 0);
    ev_child_start(loop_master, &my_w_child.watcher);

    // watcher на убийство всех worker'ов
    struct my_signal my_w_signal;
    my_w_signal.pworkers = &workers;
    ev_signal_init (&my_w_signal.watcher, signals_handler, SIGINT);
    ev_signal_init (&my_w_signal.watcher, signals_handler, SIGTERM);
    ev_signal_start(loop_master, &my_w_signal.watcher);

    while(1) {
        ev_run(loop_master, 0);
    }

    exit(EXIT_SUCCESS);
}
