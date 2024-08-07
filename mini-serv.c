#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct  s_client
{
  int             id;
  int             fd;
  struct s_client *next;
}                t_client;

t_client        *g_clients = NULL;
int             g_server_fd = -1;
int             g_max_fd = -2;
int             g_id = 0;
fd_set          g_write_set, g_read_set, g_origin_set;

void  fatal_error(void)
{
  if (g_server_fd > 0)
    close(g_server_fd);
  while (g_clients)
  {
    t_client  *tmp = g_clients->next;
    close(g_clients->fd);
    free(g_clients);
    g_clients = tmp;
  }
  write(2, "Fatal error\n", strlen("Fatal error\n"));
  exit(1);
}

int add_client(int client_fd)
{
  if (client_fd == -1)
    return 1;
  t_client *new_client = (t_client*)malloc(sizeof(t_client));
  if (!new_client)
    fatal_error();
  new_client->fd = client_fd;
  new_client->id = g_id++;
  new_client->next = g_clients;
  g_clients = new_client;
  FD_SET(client_fd, &g_origin_set);
  if (new_client->fd > g_max_fd)
    g_max_fd = new_client->fd;
  return 0;
}

void remove_client(int client_fd)
{
  t_client *current = g_clients;
  t_client *prev = NULL;
  while (current)
  {
    if (current->fd == client_fd)
    {
      if (prev)
        prev->next = current->next;
      else
        g_clients = current->next;
      FD_CLR(client_fd, &g_origin_set);
      close(client_fd);
      free(current);
      return;
    }
    prev = current;
    current = current->next;
  }
}

void  send_all(int fd, char *msg)
{
  for (int send_fd = 0; send_fd <= g_max_fd; send_fd++)
  {
    if (FD_ISSET(send_fd, &g_write_set) && send_fd != fd)
    {
      if (send(send_fd, msg, strlen(msg), 0) == -1)
        fatal_error();
    }
  }

}
/*
NOTE: More efficient version but longer
  
  void	send_all(int fd, char* msg)
  {
    t_client  *tmp;
    tmp = g_clients;
    while (tmp)
    {
      if (FD_ISSET(tmp->fd, &g_write_set) && tmp->fd != fd)
      {
        if (send(tmp->fd, msg, strlen(msg), 0) == -1)
          fatal_error();
      }
      tmp = tmp->next;
    }
  }
*/

void  broadcast(int fd, char* msg, int is_serv)
{

  if (is_serv)
  {
    char buff_send[256064];
    sprintf(buff_send, "server: %s", msg);
    send_all(fd, buff_send);
  }
  else
  send_all(fd, msg);
}

int main(int ac, char **av)
{
  if (ac != 2)
  {
    write(2, "Wrong number of arguments\n", strlen("Wrong number of arguments\n"));
    exit(1);
  }
  struct sockaddr_in  servaddr; 
  t_client            *current_client = NULL;
  t_client            *next_client = NULL;
  socklen_t           sockaddrlen = 0;
  char                recv_buffer[256000];
  char                send_buffer[256032];
  int                 bytes_read = 0;

  bzero(&servaddr, sizeof(servaddr)); 
  FD_ZERO(&g_origin_set);
  g_server_fd = socket(AF_INET, SOCK_STREAM, 0); 
  if (g_server_fd == -1)
    fatal_error();

  servaddr.sin_family = AF_INET; 
  servaddr.sin_addr.s_addr = htonl(2130706433);
  servaddr.sin_port = htons(atoi(av[1])); 

  if ((bind(g_server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0 || listen(g_server_fd, 10) != 0)
    fatal_error();

  FD_SET(g_server_fd, &g_origin_set);
  g_max_fd = g_server_fd;
  sockaddrlen = sizeof(servaddr);
  while (1)
  {
    g_read_set = g_write_set = g_origin_set;
    if (select(g_max_fd + 1, &g_read_set, &g_write_set, 0, 0) == -1)
      continue;
    if (FD_ISSET(g_server_fd, &g_read_set))
    {
      if (add_client(accept(g_server_fd, (struct sockaddr *)&servaddr, &sockaddrlen)) != 0)
        continue;
      sprintf(send_buffer, "client %d just arrived\n", g_clients->id);
      broadcast(g_clients->id, send_buffer, 1);
    }
    current_client = g_clients;
    while (current_client)
    {
      next_client = current_client->next;
      if (FD_ISSET(current_client->fd, &g_read_set))
      {
        int bytes_read = recv(current_client->fd, recv_buffer, sizeof(recv_buffer) -1, 0);
        if (bytes_read <= 0)
        {
          remove_client(current_client->fd);
          sprintf(send_buffer, "client %d just left\n", current_client->fd);
          broadcast(-1, send_buffer, 1);
        }
        else
        {
          recv_buffer[bytes_read] = 0;
          sprintf(send_buffer, "client %d: %s", current_client->id, recv_buffer);
          broadcast(current_client->fd, send_buffer, 0);
        }
      }
      current_client = next_client;
    }
  }
}
