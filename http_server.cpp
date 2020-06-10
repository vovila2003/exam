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
#include <fcntl.h>



// std::map<pid_t, int> workers; // список worker'ов и номера soketpair к ним

// ev_io *w_fd;
// ev_io *w_client;
// ev_signal *w_signal_term;
// ev_signal *w_signal_int;
// ev_io* w_accept;
// ev_child* w_child;

struct my_io {
    struct ev_io watcher;
    int sock;
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
/*
static void  child_signal_handler(struct ev_loop *loop, ev_signal *w, int revents) {
    ev_signal_stop(loop, w_signal_term);
    ev_signal_stop(loop, w_signal_int);
    ev_io_stop(loop, w_fd);
    ev_break(loop, EVBREAK_ALL);
    free(w_fd);
    free(w_client);
    free(w_signal_term);
    free(w_signal_int);
    std::cout << "Worker: process " << getpid() << " die "<< std::endl;
    std::cout << std::flush;
    exit(EXIT_FAILURE);
}
*/



void read_cb (struct ev_loop *loop, struct ev_io *watcher, int revents) {
  /*  char buffer[1024];
    size_t r = recv(watcher->fd, buffer, 1024, MSG_NOSIGNAL);
    if (r < 0) 
        return;
    else if (r == 0) {
        ev_io_stop(loop, watcher);
        free(watcher);
        return;
    } else {
        send(watcher->fd, buffer, r, MSG_NOSIGNAL);
    }
*/
}

/*
static void newfd_cb(struct ev_loop *loop, struct ev_io *watcher, int revents){
    struct my_io *w = (struct my_io*) watcher;
   
    int fd;
    char buf[16];
    ssize_t size;
    // int sock = w->sock;
    // std::cout << "Socket2 = " << sock << std::endl << std::flush;
    size = sock_fd_read(sock, buf, sizeof(buf), &fd);
    if (fd != -1) {
        // обработка входящего дискриптора
        std::cout << "Worker (newfd_cb):процесс " << getpid() << " принял дескриптор " << fd << std::endl;   
        std::cout << std::flush;
        char buffer[3];
        strcpy(buffer, "Hi");
        buffer[3] = '\0';
        send(fd, buffer, 3, MSG_NOSIGNAL);




        // w_client = (struct ev_io*) malloc(sizeof(struct ev_io));
        // ev_io_init(w_client, read_cb, fd, EV_READ);
        // ev_io_start(loop, w_client);



/*
    char buffer[1024];
    size_t r = recv(watcher->fd, buffer, 1024, MSG_NOSIGNAL);
    if (r < 0) 
        return;
    else if (r == 0) {
        ev_io_stop(loop, watcher);
        free(watcher);
        return;
    } else {
        send(watcher->fd, buffer, r, MSG_NOSIGNAL);
    }
*/
/*
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
   
}

*/

void childprocess(int socket) {
    /*loop_child = ev_default_loop(0);
    if (!loop_child) {
        perror("Error! Can't create default loop in child!");
        exit(EXIT_FAILURE);
    }

     
    struct my_io my_w_fd;
    my_w_fd.sock = socket;
    std::cout << "Socket1 = " << socket << std::endl << std::flush;

    // watcher на прием нового дескриптора
    // w_fd = (struct ev_io*) malloc(sizeof(struct ev_io));
    // ev_io_init(w_fd, newfd_cb, socket, EV_READ);

    ev_io_init(&my_w_fd.watcher, newfd_cb, socket, EV_READ);
    ev_io_start(loop_child, &my_w_fd.watcher);
    // watchers на сигналы остановки worker'а
    w_signal_term = (struct ev_signal*) malloc(sizeof(struct ev_signal));
    w_signal_int = (struct ev_signal*) malloc(sizeof(struct ev_signal));
    ev_signal_init (w_signal_term, child_signal_handler, SIGTERM);
    ev_signal_init (w_signal_int, child_signal_handler, SIGINT);
    ev_signal_start(loop_child, w_signal_term);
    ev_signal_start(loop_child, w_signal_int);
    std::cout << "Worker # " << getpid() << " starts event loop" << std::endl;
    std::cout << std::flush;
    while(true) {
        ev_run(loop_child, 0);
    }*/




// ----test-----------------------------------------
    int fd;
    char buf[16];
    ssize_t size;
    char buffer[4];
    strcpy(buffer, "Hi\n");
    buffer[4] = '\0';
    std::cerr << "Процесс " << getpid() << " запустился, сокет = " << socket << std::endl << std::flush;
    for(;;) {
        size = sock_fd_read(socket, buf, sizeof(buf), &fd);
        if (size <= 0)
            break;
        if (fd != -1) {
            std::cerr << "Worker (childprocess):процесс " << getpid() << " принял дескриптор " << fd << std::endl<< std::flush;
            send(fd, buffer, 4, MSG_NOSIGNAL);
            shutdown(fd, SHUT_RDWR);
            close(fd);
            // sock_fd_write(socket, buf, sizeof(buf), &fd);
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
        // sock_fd_write(sv[0], (void*) "1", 1, -1);
    } else if (pid == 0){ // child
        close(sv[0]);
        childprocess(sv[1]);
    }
}
/*
static void sigchld_handler(struct ev_loop *loop, ev_child *w, int revents) { // действия при смерти worker
    int pid = w->rpid;

    if (pid < 0) {
        perror("Error on waitpid");
    } else {
        //закрываем socketpair
        close(workers[pid]);
        // erase from map
        workers.erase(workers.find(pid));
        std::cout << "Master Process (sigchld_handler): pid = " << pid << " deleted " << std::endl;
        std::cout << std::flush;
        create_child(); // создаем нового worker
    }

    /*
    for (auto it = workers.begin(); it != workers.end(); it++){
        std::cout << "pid = " << it->first << "; sv = " << it->second << std::endl;
    }
    std::cout << std::endl;
    */
// }

void close_all_socketpair_and_kill_child(std::map<pid_t, int> & workers){
    for (auto it = workers.cbegin(); it != workers.cend(); it++){
        kill(SIGTERM, it->first);
        close(it->second);
    }
}

void print_all_workers(std::map<pid_t, int> & workers) {
    std::cout << "----------" << workers.size() << "----------"<< std::endl;
    for (auto it = workers.cbegin(); it != workers.cend(); it++){
        std::cout << "pid = " << it->first << "; sv = " << it->second << std::endl;
    }
    std::cout << "--------------------"<< std::endl<< std::flush;
}

void accept_cb(struct ev_loop *loop, struct ev_io * watcher, int revents) {
    struct my_io *w = (struct my_io*) watcher;
    
    static int current_worker = 0;
    // if (current_worker == 0)
    //     current_worker++;

    std::cout << "current_worker = " << current_worker << std::endl << std::flush;
    int client_sd = accept(w->watcher.fd, 0, 0);
    if (client_sd <= 0) {
        return;
    }
//test
    // print_all_workers(*(w->pworkers)); // test
//test end    
    auto it = w->pworkers->cbegin();
    for (int i = 0; (i < current_worker) && (it!= w->pworkers->cend()); ++i)
        ++it;
    std::cout << "first = " << it->first << ", second = " << it->second << std::endl << std::flush;
    sock_fd_write(it->second, (void*) "1", 1, client_sd);
    close(client_sd);

    // std::cerr << "Master Process (accept_cb): процессу " << it->first 
    //           << " отправил fd " << client_sd << " по sv " << it->second
    //           << "; current_worker = " << current_worker << std::endl << std::flush;
    current_worker = (current_worker + 1) % w->pworkers->size();
}

int main(int argc, char const *argv[])
{
	
    // распарсить строку
    char ip[INET_ADDRSTRLEN];
    strcpy(ip,"127.0.0.1");
    int port = 12345;


    // создаем демона


    // MASTER PROCESS

    // создаем сокет
    int master_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);//tcp
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    int res = inet_pton(AF_INET, ip, &addr.sin_addr);

    if (res < 0) {
        perror("Error! Incorrect ip-address!");
        close(master_socket); // закрываем socket
        exit(EXIT_FAILURE);
    }

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
    print_all_workers(workers);

    struct my_io my_w_accept;
    my_w_accept.sock = 0;
    my_w_accept.pworkers = &workers;
    ev_io_init(&my_w_accept.watcher, accept_cb, master_socket, EV_READ);
    ev_io_start(loop_master, &my_w_accept.watcher);
/*
    w_child = (ev_child*) malloc(sizeof(struct ev_child));
    ev_child_init (w_child, sigchld_handler, 0, 0);
    ev_child_start(loop_master, w_child);
*/

    // ev_io w_close_fd;
    // ev_io_init(&w_close_fd, close_fd_cd, 
    // ev_io_start ev_io


    while(1) {
        ev_run(loop_master, 0);
    }

    return 0;
}
