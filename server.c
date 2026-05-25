#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 33333
#define MAX_BUFFER 4096
#define MAX_ROOMS 2
#define MAX_CLIENTS 20

struct _client_info
{
	struct sockaddr_in * addr;
	int sck;
	char name[21];
	short status; // -2 = espaço livre, -1 = falta nome, 0 = ativo, 1 = dentro de sala ...
	short id;
	short room_id;
};

struct _room_info
{
	pthread_mutex_t lock;
	char name[21];
	char password[11]; // não implementado
	short clients_id[MAX_CLIENTS];
	short banned_id[MAX_CLIENTS];
	short muted_id[MAX_CLIENTS];
	short inv_id[MAX_CLIENTS];
	short num_clients;
	short num_banned;
	short num_muted;
	short private;
	short admin;
	short pswrd; // não implementado
	short active;
};

typedef struct _client_info CLIENT_INFO;
typedef struct _room_info ROOM_INFO;

/**************************** Variables *************************/

CLIENT_INFO clt_info[MAX_CLIENTS];
ROOM_INFO rmm_info[MAX_ROOMS];

struct sockaddr_in s_addrs[MAX_CLIENTS + 1];
socklen_t addr_size = sizeof(struct sockaddr_in);

short active_clients = 0; 
short active_rooms = 0;
short bnd, lst, empty = 0; 
short empty_r = 0;

int scks[MAX_CLIENTS + 1];

pthread_t thr[MAX_CLIENTS];
pthread_t screen;

pthread_mutex_t client_lock;
pthread_mutex_t room_lock;
/****************************************************************/

void check (int err, char * msg)
{
	if (err < 0) 
	{
		puts(msg);
		exit(1);
	}
}

int get_command (char * buffer)
{
	char copy[MAX_BUFFER];
	strcpy(copy, buffer);

	if (!strcmp(strtok(copy, " "), "/exit")) return 1;
	if (!strcmp(strtok(copy, " "), "/connect")) return 2;
	if (!strcmp(strtok(copy, " "), "/disconnect")) return 3;
	if (!strcmp(strtok(copy, " "), "/create")) return 4;
	if (!strcmp(strtok(copy, " "), "/nickname")) return 5;
	if (!strcmp(strtok(copy, " "), "/leave")) return 6;
	if (!strcmp(strtok(copy, " "), "/join")) return 7;
	if (!strcmp(strtok(copy, " "), "/mute")) return 8;
	if (!strcmp(strtok(copy, " "), "/unmute")) return 9;
	if (!strcmp(strtok(copy, " "), "/kick")) return 10;
	if (!strcmp(strtok(copy, " "), "/create_private")) return 11;
	if (!strcmp(strtok(copy, " "), "/invite")) return 12;
	if (!strcmp(strtok(copy, " "), "/whois")) return 13;
	if (!strcmp(strtok(copy, " "), "/ping")) return 14;
	if (!strcmp(strtok(copy, " "), "/list_clients")) return 15;
	if (!strcmp(strtok(copy, " "), "/list_rooms")) return 16;
	return 0;
}

void send_frac_msg (char * msg, int len, int sck, int i)
{
	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);
	
	while (len - i + 1 > MAX_BUFFER)
	{
		bzero(buffer, MAX_BUFFER);
		memcpy(buffer, "/F ", 3);
		memcpy(buffer + 3, msg + i, MAX_BUFFER - 4);
			
		buffer[MAX_BUFFER - 1] = '\0';
		i += MAX_BUFFER - 4;
			
		send(sck, buffer, MAX_BUFFER, 0);
		//puts(buffer);
	}
		
	memcpy(buffer, msg + i, len - i + 1);
	send(sck, buffer, MAX_BUFFER, 0);
	//puts(buffer);
}

void init_server (struct sockaddr_in * srv)
{
	(*srv).sin_family = AF_INET;
	(*srv).sin_port = SERVER_PORT;
	inet_aton(SERVER_IP, &(*srv).sin_addr);
}

void init_infos ()
{
	pthread_mutex_init(&client_lock, NULL);
	pthread_mutex_init(&room_lock, NULL);
	
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		clt_info[i].status = -2;
		clt_info[i].addr = NULL;
		clt_info[i].name[0] = '\0';
	}
	
	for (int i = 0; i < MAX_ROOMS; i++)
	{
		pthread_mutex_init(&rmm_info[i].lock, NULL);
		rmm_info[i].name[0] = '\0';
		rmm_info[i].num_clients = 0;
		rmm_info[i].num_banned = 0;
		rmm_info[i].num_muted = 0;
		rmm_info[i].private = 0;
		rmm_info[i].pswrd = 0;
		rmm_info[i].active = -1;
		
		for (int j = 0; j < MAX_CLIENTS; j++)
		{
			rmm_info[i].clients_id[j] = 0;
			rmm_info[i].banned_id[j] = 0;
			rmm_info[i].muted_id[j] = 0;
			rmm_info[i].inv_id[j] = 0;
		}
	}
}


int find_empty (int mode)
{
	if (mode == 1)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (clt_info[i].status == -2) return i;
		}
	}

	if (mode == 2)
	{
		for (int i = 0; i < MAX_ROOMS; i++)
		{
			if (rmm_info[i].active == -1) return i;
		}
	}
	
	return -1;
}

int search_room (char * name)
{
	for (int i = 0; i < MAX_ROOMS; i++)
	{
		if (rmm_info[i].active == 1 && !strcmp(rmm_info[i].name, name))
		{
			return i;
		}
	}

	return -1;
}

int search_client (char * name)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (clt_info[i].status != -2 && !strcmp(clt_info[i].name, name))
		{
			return i;
		}
	}

	return -1;
}

void create_room (CLIENT_INFO * clt, char * msg, int private)
{
	char buffer[MAX_BUFFER];

	pthread_mutex_lock(&room_lock);
	
	if (active_rooms == MAX_ROOMS)
	{
		memcpy(buffer, "/E_NOSPACE\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if ((*clt).status == -1)
	{
		memcpy(buffer, "/E_REQNAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if ((*clt).status == 1)
	{
		memcpy(buffer, "/E_AINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (strlen(msg) <= 8)
	{
		memcpy(buffer, "/E_NOONAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	char * name = NULL;
	
	if (!private) name = strtok(msg + 8, " ");
	else          name = strtok(msg + 16, " ");
	
	if (search_room(name) != -1)
	{
		memcpy(buffer, "/E_INVNAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		
		char port_s[MAX_BUFFER];
		
		sprintf(port_s, "%d", clt_info[(*clt).id].addr->sin_port);
	
		(*clt).status = 1;
		(*clt).room_id = empty_r;
		
		memcpy(buffer, "/S_CRTROOM\0", 11);
		memcpy(buffer + 11, name, strlen(name) + 1);
		memcpy(buffer + 12 + strlen(name), port_s, strlen(port_s) + 1);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		
		rmm_info[empty_r].active = 1;
		rmm_info[empty_r].clients_id[(*clt).id] = 1;
		rmm_info[empty_r].banned_id[(*clt).id] = 0;
		rmm_info[empty_r].num_clients = 1;
		rmm_info[empty_r].num_banned = 0;
		rmm_info[empty_r].num_muted = 0;
		rmm_info[empty_r].private = private;
		rmm_info[empty_r].admin = (*clt).id;
		memcpy(rmm_info[empty_r].name, name, strlen(name) + 1);
		
		active_rooms++;
		empty_r = find_empty(2);
	}
	
	pthread_mutex_unlock(&room_lock);
}

void set_name (CLIENT_INFO * clt, char * msg) // falta checar limite
{
	char buffer[MAX_BUFFER];
	
	if (strlen(msg) <= 10)
	{
		memcpy(buffer, "/E_NOONAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	char * name = strtok(msg + 10, " ");
	
	if (search_client(name) != -1)
	{
		memcpy(buffer, "/E_INVNAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	else
	{
		memcpy(buffer, "/S_SETNAME\0", 11);
		memcpy(buffer + 11, (*clt).name, strlen((*clt).name) + 1);
		memcpy(buffer + 12 + strlen((*clt).name), name, strlen(name) + 1);
		
		memcpy((*clt).name, name, strlen(name) + 1);
		(*clt).status = 0;
		
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
}

void leave_room (CLIENT_INFO * clt)
{
	char buffer[MAX_BUFFER];

	if ((*clt).status != 1)
	{
		memcpy(buffer, "/E_NINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (rmm_info[(*clt).room_id].admin == (*clt).id)
	{
		pthread_mutex_lock(&room_lock);
		pthread_mutex_lock(&rmm_info[(*clt).room_id].lock);
		
		memcpy(buffer, "/N_DELROOM\0", 11);
		
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (rmm_info[(*clt).room_id].clients_id[i] == 1)
			{
				rmm_info[(*clt).room_id].clients_id[i] = 0;
					
				clt_info[i].status = 0;
				send(clt_info[i].sck, buffer, MAX_BUFFER, 0);
			}
			
			rmm_info[(*clt).room_id].banned_id[i] = 0;
			rmm_info[(*clt).room_id].muted_id[i] = 0;
		}
			
		rmm_info[(*clt).room_id].active = -1;
		
		active_rooms--;
		empty_r = (*clt).room_id;
			
		pthread_mutex_unlock(&room_lock);
		pthread_mutex_unlock(&rmm_info[(*clt).room_id].lock);
			
		return;
	}
	
	pthread_mutex_lock(&rmm_info[(*clt).room_id].lock);
	
	rmm_info[(*clt).room_id].num_clients--;
	rmm_info[(*clt).room_id].clients_id[(*clt).id] = 0;
	(*clt).status = 0;
	
	memcpy(buffer, "/N_EXTROOM\0", 11);
	send((*clt).sck, buffer, MAX_BUFFER, 0);
	
	pthread_mutex_unlock(&rmm_info[(*clt).room_id].lock);
}

void join_room (CLIENT_INFO * clt, char * msg)
{
	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);
	
	if ((*clt).status == -1)
	{
		memcpy(buffer, "/E_REQNAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}

	if ((*clt).status == 1)
	{
		memcpy(buffer, "/E_AINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (strlen(msg) <= 6)
	{
		memcpy(buffer, "/E_NOONAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}

	int room_id = search_room(strtok(msg + 6, " "));
	
	if (room_id == -1)
	{
		memcpy(buffer, "/E_NTFOUND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (rmm_info[room_id].private && !rmm_info[room_id].inv_id[(*clt).id])
	{
		memcpy(buffer, "/E_PRVROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (rmm_info[room_id].banned_id[(*clt).id])
	{
		memcpy(buffer, "/E_IDBANND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	pthread_mutex_lock(&rmm_info[room_id].lock);
	
	// o admin pode ter fechado a sala enquanto o servidor verifica...
	// por isso a sala deve ser checada novamente
	
	if (!rmm_info[room_id].active)
	{
		memcpy(buffer, "/E_NTFOUND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		memcpy(buffer, "/S_JONROOM\0", 11);
		memcpy(buffer + 11, rmm_info[room_id].name, strlen(rmm_info[room_id].name) + 1);
		
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		
		rmm_info[room_id].num_clients++;
		rmm_info[room_id].clients_id[(*clt).id] = 1;
		(*clt).room_id = room_id;
		(*clt).status = 1;
	}
	
	pthread_mutex_unlock(&rmm_info[room_id].lock);
}

void mute (CLIENT_INFO * clt, char * msg)
{
	char buffer[MAX_BUFFER];
	
	if ((*clt).status != 1)
	{
		memcpy(buffer, "/E_NINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (rmm_info[(*clt).room_id].admin != (*clt).id)
	{
		memcpy(buffer, "/E_NTADMIN\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (strlen(msg) <= 6)
	{
		memcpy(buffer, "/E_NOONAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	pthread_mutex_lock(&client_lock);
	
	int clt_id = search_client(strtok(msg + 6, " "));
	
	if (clt_id == -1 || !rmm_info[(*clt).room_id].clients_id[clt_id])
	{
		memcpy(buffer, "/E_NTFOUND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (clt_id == (*clt).id)
	{
		memcpy(buffer, "/E_IDINVAL\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (rmm_info[(*clt).room_id].muted_id[clt_id])
	{
		memcpy(buffer, "/E_ALRMUTE\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		pthread_mutex_lock(&rmm_info[(*clt).room_id].lock);
	
		rmm_info[(*clt).room_id].muted_id[clt_id] = 1;
		rmm_info[(*clt).room_id].num_muted++;
		
		bzero(buffer, MAX_BUFFER);
		memcpy(buffer, "/S_CLTMUTE\0", 11);
		memcpy(buffer + 11, clt_info[clt_id].name, strlen(clt_info[clt_id].name) + 1);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		
		buffer[1] = 'N';
		send(clt_info[clt_id].sck, buffer, MAX_BUFFER, 0);
		
		pthread_mutex_unlock(&rmm_info[(*clt).room_id].lock);
	}
	
	pthread_mutex_unlock(&client_lock);
}

void unmute (CLIENT_INFO * clt, char * msg)
{
	char buffer[MAX_BUFFER];
	
	if ((*clt).status != 1)
	{
		memcpy(buffer, "/E_NINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (rmm_info[(*clt).room_id].admin != (*clt).id)
	{
		memcpy(buffer, "/E_NTADMIN\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (strlen(msg) <= 8)
	{
		memcpy(buffer, "/E_NOONAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	pthread_mutex_lock(&client_lock);
	
	int clt_id = search_client(strtok(msg + 8, " "));
	
	if (clt_id == -1)
	{
		memcpy(buffer, "/E_NTFOUND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (clt_id == (*clt).id)
	{
		memcpy(buffer, "/E_IDINVAL\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (!rmm_info[(*clt).room_id].muted_id[clt_id])
	{
		memcpy(buffer, "/E_ALRUNMT\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		pthread_mutex_lock(&rmm_info[(*clt).room_id].lock);
	
		rmm_info[(*clt).room_id].muted_id[clt_id] = 0;
		rmm_info[(*clt).room_id].num_muted--;
		
		bzero(buffer, MAX_BUFFER);
		memcpy(buffer, "/S_CLTUNMT\0", 11);
		memcpy(buffer + 11, clt_info[clt_id].name, strlen(clt_info[clt_id].name) + 1);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		
		buffer[1] = 'N';
		send(clt_info[clt_id].sck, buffer, MAX_BUFFER, 0);
		
		pthread_mutex_unlock(&rmm_info[(*clt).room_id].lock);
	}
	
	pthread_mutex_unlock(&client_lock);
}

void kick (CLIENT_INFO * clt, char * msg)
{
	char buffer[MAX_BUFFER];
	
	if ((*clt).status != 1)
	{
		memcpy(buffer, "/E_NINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (rmm_info[(*clt).room_id].admin != (*clt).id)
	{
		memcpy(buffer, "/E_NTADMIN\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (strlen(msg) <= 6)
	{
		memcpy(buffer, "/E_NOONAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	pthread_mutex_lock(&client_lock);
	
	int clt_id = search_client(strtok(msg + 6, " "));
	
	if (clt_id == -1)
	{
		memcpy(buffer, "/E_NTFOUND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (clt_id == (*clt).id)
	{
		memcpy(buffer, "/E_IDINVAL\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (clt_info[clt_id].room_id != (*clt).room_id)
	{
		memcpy(buffer, "/E_NTFOUND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		leave_room(&clt_info[clt_id]);
	
		pthread_mutex_lock(&rmm_info[(*clt).room_id].lock);
	
		rmm_info[(*clt).room_id].banned_id[clt_id] = 1;
		rmm_info[(*clt).room_id].num_banned++;
		
		pthread_mutex_unlock(&rmm_info[(*clt).room_id].lock);
		
		bzero(buffer, MAX_BUFFER);
		sprintf(buffer, "%s %s", "/S_CLTBNND", clt_info[clt_id].name);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		
		buffer[1] = 'N';
		send(clt_info[clt_id].sck, buffer, MAX_BUFFER, 0);
	}
	
	pthread_mutex_unlock(&client_lock);
}

void invite (CLIENT_INFO * clt, char * msg)
{
	char buffer[MAX_BUFFER];
	
	if ((*clt).status != 1)
	{
		memcpy(buffer, "/E_NINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (rmm_info[(*clt).room_id].admin != (*clt).id)
	{
		memcpy(buffer, "/E_NTADMIN\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (!rmm_info[(*clt).room_id].private)
	{
		memcpy(buffer, "/E_NTPRVTE\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (strlen(msg) <= 8)
	{
		memcpy(buffer, "/E_NOONAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	pthread_mutex_lock(&client_lock);
	
	int clt_id = search_client(strtok(msg + 8, " "));
	
	if (clt_id == -1)
	{
		memcpy(buffer, "/E_NTFOUND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (clt_id == (*clt).id)
	{
		memcpy(buffer, "/E_IDINVAL\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (rmm_info[(*clt).room_id].banned_id[clt_id])
	{
		memcpy(buffer, "/E_ALRBNND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (rmm_info[(*clt).room_id].inv_id[clt_id])
	{
		memcpy(buffer, "/E_ALRINVT\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		pthread_mutex_lock(&rmm_info[(*clt).room_id].lock);
		
		rmm_info[(*clt).room_id].inv_id[clt_id] = 1;
		
		bzero(buffer, MAX_BUFFER);
		sprintf(buffer, "%s %s", "/S_INVSENT", clt_info[clt_id].name);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		
		bzero(buffer, MAX_BUFFER);
		sprintf(buffer, "%s %s %s", "/N_INVRECV", (*clt).name, rmm_info[(*clt).room_id].name);
		send(clt_info[clt_id].sck, buffer, MAX_BUFFER, 0);
		
		pthread_mutex_unlock(&rmm_info[(*clt).room_id].lock);
	}
	
	pthread_mutex_unlock(&client_lock);
}

void whois (CLIENT_INFO * clt, char * msg)
{
	char buffer[MAX_BUFFER];

	if ((*clt).status != 1)
	{
		memcpy(buffer, "/E_NINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (rmm_info[(*clt).room_id].admin != (*clt).id)
	{
		memcpy(buffer, "/E_NTADMIN\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	if (strlen(msg) <= 7)
	{
		memcpy(buffer, "/E_NOONAME\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		return;
	}
	
	pthread_mutex_lock(&client_lock);
	
	int clt_id = search_client(strtok(msg + 7, " "));
	
	if (clt_id == -1 || !rmm_info[(*clt).room_id].clients_id[clt_id])
	{
		memcpy(buffer, "/E_NTFOUND\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		char * whois_ip = (char *) inet_ntoa(clt_info[clt_id].addr->sin_addr);
		int whois_port = clt_info[clt_id].addr->sin_port;
		
		bzero(buffer, MAX_BUFFER);
		sprintf(buffer, "%s %s %s %d", "/S_WHOISRQ", clt_info[clt_id].name, whois_ip, whois_port);
		
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	pthread_mutex_unlock(&client_lock);
}

void ping (CLIENT_INFO * clt)
{
	char buffer[MAX_BUFFER];
	
	memcpy(buffer, "/S_PINGREQ\0", 11);
	send((*clt).sck, buffer, MAX_BUFFER, 0);
}

void broadcast (CLIENT_INFO * clt, char * msg)
{
	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);
	
	if ((*clt).status != 1)
	{
		memcpy(buffer, "/E_NINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (rmm_info[(*clt).room_id].muted_id[(*clt).id])
	{
		memcpy(buffer, "/E_CLTMUTE\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else if (msg[0] == '/' && msg[1] == 'F')
	{
		sprintf(buffer, "[%s:] ", (*clt).name);
		
		char * full_msg = (char *) realloc (full_msg, strlen(buffer));
		memcpy(full_msg, buffer, strlen(buffer));
		
		while (recv((*clt).sck, buffer, MAX_BUFFER, 0) > 0)
		{
			char * full_msg = (char *) realloc (full_msg, strlen(full_msg) + strlen(buffer));
			memcpy(full_msg + strlen(full_msg), buffer, strlen(buffer));
			
			if (buffer[0] != '/') break;
		}
		
		send_frac_msg(full_msg, strlen(full_msg), (*clt).sck, 0);
	}
	
	else
	{
		if (rmm_info[(*clt).room_id].admin == (*clt).id)
		{
			sprintf(buffer, "[%s:] %s", (*clt).name, msg);
		}
		
		else
		{
			sprintf(buffer, "%s: %s", (*clt).name, msg);
		}
	
		pthread_mutex_lock(&rmm_info[(*clt).room_id].lock);
		
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (rmm_info[(*clt).room_id].clients_id[i])
			{
				send(clt_info[i].sck, buffer, MAX_BUFFER, 0);
			}
		}
		
		pthread_mutex_unlock(&rmm_info[(*clt).room_id].lock);
	}
}

void list_clients (CLIENT_INFO * clt)
{
	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);
	
	if ((*clt).status != 1)
	{
		memcpy(buffer, "/E_NINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		memcpy(buffer, "/S_CLTSREQ\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (clt_info[i].room_id == (*clt).room_id && clt_info[i].status == 1)
			{
				char * name = clt_info[i].name;
				short muted = rmm_info[(*clt).room_id].muted_id[i];
			
				bzero(buffer, MAX_BUFFER);
				sprintf(buffer, "|Nome: %s | Muted: %d|", name, muted);
				send((*clt).sck, buffer, MAX_BUFFER, 0);
			}
		}
		
		bzero(buffer, MAX_BUFFER);
		memcpy(buffer, "/F_CLTSREQ\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
}

void list_rooms (CLIENT_INFO * clt)
{
	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);
	
	if ((*clt).status == 1)
	{
		memcpy(buffer, "/E_AINROOM\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
	
	else
	{
		memcpy(buffer, "/S_RMMSREQ\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
		
		for (int i = 0; i < MAX_ROOMS; i++)
		{
			if (rmm_info[i].active == 1 && !rmm_info[i].private)
			{
				char * name = rmm_info[i].name;
				int num_clients = rmm_info[i].num_clients;
			
				bzero(buffer, MAX_BUFFER);
				sprintf(buffer, "|Nome: %s | Número de Clientes: %d|", name, num_clients);
				send((*clt).sck, buffer, MAX_BUFFER, 0);
			}
		}
		
		bzero(buffer, MAX_BUFFER);
		memcpy(buffer, "/F_RMMSREQ\0", 11);
		send((*clt).sck, buffer, MAX_BUFFER, 0);
	}
}

void * listen_client (void * args)
{
	char buffer[MAX_BUFFER];
	CLIENT_INFO * clt = (CLIENT_INFO *) args;
	
	char * ip = (char *) inet_ntoa(((*clt).addr)->sin_addr);
	int port = ((*clt).addr)->sin_port, err, cmd;
	
	while (recv((*clt).sck, buffer, MAX_BUFFER, 0) > 0)
	{
		cmd = get_command(buffer);
		
		if (cmd == 3) break;
		if (cmd == 4) create_room(clt, buffer, 0);
		if (cmd == 5) set_name(clt, buffer);
		if (cmd == 6) leave_room(clt);
		if (cmd == 7) join_room(clt, buffer);
		if (cmd == 8) mute(clt, buffer);
		if (cmd == 9) unmute(clt, buffer);
		if (cmd == 10) kick(clt, buffer);
		if (cmd == 11) create_room(clt, buffer, 1);
		if (cmd == 12) invite(clt, buffer);
		if (cmd == 13) whois(clt, buffer);
		if (cmd == 14) ping(clt);
		if (cmd == 15) list_clients(clt);
		if (cmd == 16) list_rooms(clt);
		if (cmd == 0) broadcast(clt, buffer);
	}
	
	pthread_mutex_lock(&client_lock);
	
	if ((*clt).status == 1) leave_room(clt);
	 
	(*clt).status = -2;
	active_clients--;
	empty = (*clt).id;
	
	for (int i = 0; i < MAX_ROOMS; i++)
	{
		pthread_mutex_lock(&rmm_info[i].lock);
	
		if (rmm_info[i].banned_id[(*clt).id])
		{
			rmm_info[i].num_banned--;
			rmm_info[i].banned_id[(*clt).id] = 0;
		}
		
		if (rmm_info[i].muted_id[(*clt).id])
		{
			rmm_info[i].num_muted--;
			rmm_info[i].muted_id[(*clt).id] = 0;
		}
		
		rmm_info[i].inv_id[(*clt).id] = 0;
		
		pthread_mutex_unlock(&rmm_info[i].lock);
	}
	
	pthread_mutex_unlock(&client_lock);
}

void manage_connection ()
{
	while (1)
	{
		if (active_clients < MAX_CLIENTS)
		{
			int new_sck = accept(scks[MAX_CLIENTS], (struct sockaddr *) &s_addrs[empty], &addr_size);
			
			pthread_mutex_lock(&client_lock);
			
			clt_info[empty].addr = &s_addrs[empty];
			clt_info[empty].sck = new_sck;
			clt_info[empty].status = -1;
			clt_info[empty].id = empty;
			memcpy(clt_info[empty].name, "Anon", 4);
			
			pthread_create(&thr[empty], NULL, &listen_client, &clt_info[empty]);
			pthread_detach(thr[empty]);
			
			active_clients++;
			empty = find_empty(1);
			
			pthread_mutex_unlock(&client_lock);
		}
	}
}

void * show_stats (void * args)
{
	while (1)
	{
		sleep(1); system("clear");
	
		puts("Servidor ONLINE.\n");
		printf("Clientes Ativos: %d/%d\n", active_clients, MAX_CLIENTS);
	
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (clt_info[i].status > -2)
			{
				printf("| Nome: %s |", clt_info[i].name);
				printf(" IP: %s |", (char *) inet_ntoa(clt_info[i].addr->sin_addr));
				printf(" Porta: %d |", clt_info[i].addr->sin_port);
				printf(" Status: %d |", clt_info[i].status);
				printf(" ID: %d |\n", clt_info[i].id);
			}
		}
		
		printf("\nSalas Ativas: %d/%d\n", active_rooms, MAX_ROOMS);
		
		for (int i = 0; i < MAX_ROOMS; i++)
		{
			if (rmm_info[i].active > -1)
			{
				printf("| Nome: %s |", rmm_info[i].name);
				printf(" Admin: %s |", clt_info[rmm_info[i].admin].name);
				printf(" Usuários: %d |", rmm_info[i].num_clients);
				printf(" Mutados: %d |", rmm_info[i].num_muted);
				printf(" Banidos: %d |", rmm_info[i].num_banned);
				printf(" Privada: %d |\n", rmm_info[i].private);
			}
		}
		
	}
}

void execute ()
{
	init_infos();
	init_server(&s_addrs[MAX_CLIENTS]);
	
	scks[MAX_CLIENTS] = socket(AF_INET, SOCK_STREAM, 0); // precisa checar
	check(scks[MAX_CLIENTS], "Socket não criado.\0");
	
	bnd = bind(scks[MAX_CLIENTS], (struct sockaddr *) &s_addrs[MAX_CLIENTS], addr_size); // precisa checar
	check(bnd, "Erro no bind.\0");
	
	lst = listen(scks[MAX_CLIENTS], 5);
	check(lst, "Erro no listen.\0");
	
	pthread_create(&screen, NULL, &show_stats, NULL);
	manage_connection();
}

int main ()
{
	/*sigset_t block_set;
	sigemptyset(&block_set);
	sigaddset(&block_set, SIGINT);
	sigprocmask(SIG_BLOCK, &block_set, NULL);*/

	execute();
	return 0;
}
