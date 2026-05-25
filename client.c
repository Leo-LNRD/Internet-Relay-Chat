#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 33333
#define MAX_BUFFER 4096

pthread_t lst;

char clt_name[MAX_BUFFER];
int end = 1, in_room = 0;

char * readline (FILE * fp, int * size)
{
	char * str = NULL;
	*size = -1;
	
	do
	{
		(*size)++;
		str = (char *) realloc (str, *size + 1);
		str[*size] = fgetc(fp);
	}
	while (str[*size] != EOF && str[*size] != '\n');
	
	if (str[*size] == EOF) *size = -1;
	str[*size] = '\0';
	
	return str;
}

void show_init ()
{
	system("clear");
	printf("\nBem-vindo, %s!\n\n", clt_name);
}

int get_command (char * buffer)
{
	char copy[MAX_BUFFER];
	strcpy(copy, buffer);

	if (!strcmp(strtok(copy, " "), "/exit")) return 1;
	if (!strcmp(strtok(copy, " "), "/connect")) return 2;
	if (!strcmp(strtok(copy, " "), "/disconnect")) return 3;
	if (buffer[0] == '/' && buffer[1] == 'F') return 4;
	else return 0;
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

void check (int err, char * msg)
{
	if (err < 0) 
	{
		puts(msg);
		exit(1);
	}
}

int check_msg (char * msg, int sck)
{
	char original[MAX_BUFFER];
	bzero(original, MAX_BUFFER);
	
	memcpy(original, msg, strlen(msg));

	if (!strcmp(strtok(msg, " "), "/E_REQNAME"))
	{
		puts("\n{Servidor:} O usuário precisa definir um nome.\n");
		return 1;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_NOONAME"))
	{
		puts("\n{Servidor:} Necessário inserir um nome após o comando.\n");
		return 2;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_SETNAME"))
	{
		char * name1 = strtok(msg + 11, " ");
		char * name2 = strtok(msg + 12 + strlen(name1), " ");
		
		memcpy(clt_name, name2, strlen(name2) + 1);
		if (!in_room) show_init();
		
		printf("{Servidor:} O nome \"%s\" foi trocado para \"%s\".\n\n", name1, name2);
		return 3;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_CRTROOM"))
	{
		char * room = strtok(msg + 11, " ");
		in_room = 1;
		
		system("clear");
		printf("\nBem-vindo à sala: \"%s\".\n\n", room);
		
		return 4;
	}
	
	if (!strcmp(strtok(msg, " "), "/N_DELROOM"))
	{
		in_room = 0;
		show_init();
		
		puts("{Servidor:} A sala em que você estava fechou.\n");
		return 5;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_NINROOM"))
	{
		puts("\n{Servidor:} Você não está em uma sala.\n");
		return 6;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_AINROOM"))
	{
	
		puts("\n{Servidor:} Você já está em uma sala.\n");
		return 7;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_NOSPACE"))
	{
		puts("\n{Servidor:} Não há espaço para criar uma nova sala.\n");
		return 8;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_JONROOM"))
	{
		in_room = 1;
		
		system("clear");
		printf("\nBem-vindo à sala: \"%s\".\n\n", strtok(msg + 11, " "));
		
		return 9;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_PRVROOM"))
	{
		puts("\n{Servidor:} Esta sala precisa de convite.\n");
		return 10;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_NTFOUND"))
	{
		puts("\n{Servidor:} O que você digitou não foi encontrado.\n");
		return 11;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_IDBANND"))
	{
		puts("\n{Servidor:} Você está banido desta sala.\n");
		return 12;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_NTADMIN"))
	{
		puts("\n{Servidor:} Você não é o administrador desta sala.\n");
		return 13;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_ALRMUTE"))
	{
		puts("\n{Servidor:} O usuário já foi mutado.\n");
		return 14;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_ALRUNMT"))
	{
		puts("\n{Servidor:} O usuário não precisa ser desmutado.\n");
		return 15;
	}
	
	if (!strcmp(strtok(msg, " "), "/N_CLTUNMT"))
	{
		printf("\n{Servidor:} O administrador desmutou o usuário \"%s\".\n\n", strtok(msg + 11, " "));
		return 16;
	}
	
	if (!strcmp(strtok(msg, " "), "/N_CLTMUTE"))
	{
		printf("\n{Servidor:} O administrador mutou o usuário \"%s\".\n\n", strtok(msg + 11, " "));
		return 17;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_CLTMUTE"))
	{
		printf("\n{Servidor:} Você mutou o usuário \"%s\".\n\n", strtok(msg + 11, " "));
		return 18;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_CLTMUTE"))
	{
		puts("\n{Servidor:} O administrador desta sala te mutou.\n");
		return 19;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_ALRBNND"))
	{
		puts("\n{Servidor:} Este usuário já foi banido.\n");
		return 20;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_CLTUNMT"))
	{
		printf("\n{Servidor:} Você desmutou o usuário \"%s\".\n\n", strtok(msg + 11, " "));
		return 21;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_NTPRVTE"))
	{
		puts("\n{Servidor:} Esta sala não é privada.\n");
		return 22;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_ALRINVT"))
	{
		puts("\n{Servidor:} Este usuário já foi convidado.\n");
		return 23;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_INVSENT"))
	{
		printf("\n{Servidor:} Convite enviado para \"%s\".\n\n", strtok(msg + 11, " "));
		return 24;
	}
	
	if (!strcmp(strtok(msg, " "), "/N_INVRECV"))
	{
		char * adm = strtok(msg + 11, " ");
		char * room = strtok(msg + 12 + strlen(adm), " ");
	
		printf("\n{Servidor:} Você recebeu um convite de \"%s\" para a sala \"%s\".\n\n", adm, room);
		return 25;
	}
	
	if (!strcmp(strtok(msg, " "), "/N_EXTROOM"))
	{
		in_room = 0;
		show_init();
		
		puts("{Servidor:} Você saiu da sala.\n");
		return 26;
	}
	
	if (!strcmp(strtok(msg, " "), "/N_CLTBNND"))
	{
		printf("\n{Servidor:} O usuário \"%s\" foi banido da sala.\n\n", strtok(msg + 11, " "));
		return 27;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_IDINVAL"))
	{
		puts("\n{Servidor:} Você não pode usar este comando em si mesmo.\n");
		return 28;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_PINGREQ"))
	{
		puts("\n{Servidor:} Pong.\n");
		return 29;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_CLTBNND"))
	{
		printf("\n{Servidor:} Você baniu o usuário \"%s\".\n\n", strtok(msg + 11, " "));
		return 30;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_WHOISRQ"))
	{
		char * name = strtok(msg + 11, " ");
		char * whois_ip = strtok(msg + 12 + strlen(name), " ");
		int whois_port = atoi(strtok(msg + 13 + strlen(name) + strlen(whois_ip), " "));
		
		printf("\n{Servidor:} O usuário \"%s\" possui o endereço \"%s:%d\".\n\n", name, whois_ip, whois_port);
		return 31;
	}
	
	if (!strcmp(strtok(msg, " "), "/E_INVNAME"))
	{
		puts("\n{Servidor:} O nome escolhido já foi usado.\n");
		return 32;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_CLTSREQ"))
	{
		char clt_infos[MAX_BUFFER];
		bzero(clt_infos, MAX_BUFFER);
		
		puts("\nLista de Clientes nesta Sala:");
		
		while (recv(sck, clt_infos, MAX_BUFFER, 0) > 0)
		{
			if (!strcmp(clt_infos, "/F_CLTSREQ")) {puts(""); break;}
			else puts(clt_infos);
		}
		
		return 33;
	}
	
	if (!strcmp(strtok(msg, " "), "/S_RMMSREQ"))
	{
		char rmm_infos[MAX_BUFFER];
		bzero(rmm_infos, MAX_BUFFER);
		
		puts("\nLista de Salas Disponíveis:");
		
		while (recv(sck, rmm_infos, MAX_BUFFER, 0) > 0)
		{
			if (!strcmp(rmm_infos, "/F_RMMSREQ")) {puts(""); break;}
			else puts(rmm_infos);
		}
		
		return 33;
	}
	
	else
	{
		puts(original);
		return 0;
	}
	
}

void init_server (struct sockaddr_in * srv)
{
	(*srv).sin_family = AF_INET;
	(*srv).sin_port = SERVER_PORT;
	inet_aton(SERVER_IP, &(*srv).sin_addr);
}

void * listen_server (void * args)
{
	char buffer[MAX_BUFFER];
	int sck = *((int *) args), err;
	while (recv(sck, buffer, MAX_BUFFER, 0) > 0) err = check_msg(buffer, sck);
}

void manage_connection (int sck)
{
	memcpy(clt_name, "Anon\0", 5);
	show_init();
	
	char * msg = NULL;
	int size, cmd;
	
	pthread_create(&lst, NULL, &listen_server, (void *) &sck);
	pthread_detach(lst);
	
	while (1)
	{
		msg = readline(stdin, &size);
		cmd = get_command(msg);
		
		if (cmd == 1 || size == -1) {send(sck, "/disconnect", MAX_BUFFER, 0); end = 0; break;}
		if (cmd == 3) {send(sck, "/disconnect", MAX_BUFFER, 0); break;}
		
		if (cmd == 4 && size < MAX_BUFFER) 
		{
			puts("\n{Servidor:} Não se pode enviar mensagens começando com \"/F\".\n");
		}
		
		if (cmd == 4 && size >= MAX_BUFFER) puts("\n{Servidor:} Mensagem ignorada.\n");
		else send(sck, msg, MAX_BUFFER, 0);
	}
	
	close(sck);
}

void execute ()
{
	system("clear");	

	int sck, cnn, cmd, size;
	struct sockaddr_in srv;
	init_server(&srv);
	
	while (end)
	{
		puts("\nNecessário conectar-se com o comando \"/connect\".\n");
		
		char * msg = readline(stdin, &size); 
		cmd = get_command(msg);
		
		if (cmd == 1) end = 0;
		if (cmd == 2)
		{
			sck = socket(AF_INET, SOCK_STREAM, 0);
			check(sck, "Socket não criado.\0");
			
			cnn = connect(sck, (struct sockaddr *) &srv, sizeof(srv)); // precisa checar
			check(cnn, "Erro na conexão.\0");
			
			manage_connection(sck);
		}
	}
}

int main ()
{
	/*sigset_t block_set;
	sigemptyset(&block_set);
	sigaddset(&block_set, SIGINT);
	sigprocmask(SIG_BLOCK, &block_set, NULL);
	*/
	
	execute();
	return 0;
}
