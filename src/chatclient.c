#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>

#define PORT 10140
#define MAXCLIENTS 5

volatile sig_atomic_t flag = 0;

void menu()
{
  flag = 1;
}

int main(int argc, char **argv)
{
  int sock, sock_udp, valid = 1;
  struct sockaddr_in host;
  struct sockaddr_in sender;
  struct hostent *hp;
  struct sockaddr_in svr;
  struct sockaddr_in clt;
  int clen, nbytes, reuse, state, sender_len;
  char rbuf[1256], sbuf[1024], nbuf[128], username[128], hostname[128];
  char req_accept[] = ("REQUEST ACCEPTED\n");
  char name_register[] = ("USERNAME REGISTERED\n");
  char req_udp[] = "CONNECT REQUEST";
  char accept_udp[] = "CONNECT ACCEPTED";
  char req_list[] = "/list\n";
  char seqret_key[] = "/send";
  fd_set rfds;
  struct timeval tv;

  if (signal(SIGINT, menu) == SIG_ERR)
  {
    perror("signal failed.");
    exit(1);
  }

  if (argc == 2)
  {
    if ((sock_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
      perror("socket");
      exit(1);
    }
    bzero(&svr, sizeof(svr));
    svr.sin_family = AF_INET;
    svr.sin_addr.s_addr = htons(INADDR_ANY); /*受付側のIPアドレスは任意*/
    svr.sin_port = htons(PORT);              /*ポート番号10140を介して受け付ける*/
    /*ソケットにソケットアドレスを割り当てる*/
    if (bind(sock_udp, (struct sockaddr *)&svr, sizeof(svr)) < 0)
    {
      perror("bind");
      exit(1);
    }
    /* host(ソケットの接続先)の情報設定*/
    bzero(&host, sizeof(host));
    host.sin_family = AF_INET;
    host.sin_port = htons(PORT);
    host.sin_addr.s_addr = inet_addr("255.255.255.255");
    setsockopt(sock_udp, SOL_SOCKET, SO_BROADCAST, &valid, sizeof(valid));
    /*パケットの送信*/
    sendto(sock_udp, req_udp, strlen(req_udp), 0, (struct sockaddr *)&host, sizeof(host));
    FD_SET(sock_udp, &rfds);
    /* 監視する待ち時間を 1 秒に設定 */
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    /* 標準入力とソケットからの受信を同時に監視する */
    int i;
    for (i = 0; i < 2; i++)
    {
      if (select(sock_udp + 1, &rfds, NULL, NULL, &tv) > 0)
      {
        if (FD_ISSET(sock_udp, &rfds))
        {
          //受け入れるパケットの送信元を指定(0.0.0.0で任意のアドレスからのパケットを受信)
          bzero(&sender, sizeof(sender));
          sender_len = sizeof(sender);
          if ((nbytes = recvfrom(sock_udp, rbuf, sizeof(rbuf), 0,
                                 (struct sockaddr *)&sender, &sender_len)) < 0)
          {
            perror("recvfrom");
            exit(1);
          }
          rbuf[16] = '\0';
          if (strcmp(rbuf, accept_udp) == 0)
          {
            break;
          }
        }
      }
    }
    if (i == 2)
    {
      close(sock_udp);
      printf("server not found\n");
      exit(0);
    }
    close(sock_udp);
  }
  else if (argc != 3)
  {
    fprintf(stderr, "Usage: %s hostname username\n", argv[0]);
    exit(1);
  }

  /**状態を1で初期化*/
  state = 1;

  do
  {
    switch (state)
    {

    case 1:
      /* 初期状態1処理 */
      /*ソケットの生成*/
      if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
      {
        perror("socket");
        exit(1);
      }
      /* host(ソケットの接続先)の情報設定*/
      bzero(&host, sizeof(host));
      host.sin_family = AF_INET;
      host.sin_port = htons(PORT);
      if (argc > 2)
      {
        if ((hp = gethostbyname(argv[1])) == NULL)
        {
          fprintf(stderr, "unknown host %s\n", argv[1]);
          exit(1);
        }
        strcpy(hostname, argv[1]);
        bcopy(hp->h_addr, &host.sin_addr, hp->h_length);
      }
      else
      {/*自動検出したサーバを設定*/
        host.sin_addr = sender.sin_addr;
        hp = gethostbyaddr((const char *)&sender.sin_addr,
                           sizeof(sender.sin_addr), AF_INET);
        if (hp == NULL)
        {
          herror("gethostbyaddr");
          return 1;
        }
        strcpy(hostname, hp->h_name);
      }

      /*サーバ側のソケットに接続*/
      if ((connect(sock, (struct sockaddr *)&host, sizeof(host))) < 0)
      {
        perror("connect");
        exit(1);
      }
      else
      {
        printf("connected to %s\n", hostname);
      }
      state = 2;

      break;

    case 2:
      /* 状態2処理 */
      if ((nbytes = read(sock, rbuf, 17)) < 0)
      {
        perror("read");
      }
      else
      {
        rbuf[nbytes] = '\0';
        if (strncmp(rbuf, req_accept, 17) == 0)
        {
          printf("join request accepted\n");
          state = 3;
        }
        else
          state = 6;
      }
      break;

    case 3:
      /* 状態3処理 */
      /*ユーザー名に改行追加*/
      if (argc > 2)
      {
        int len = strlen(argv[2]);
        char username[len + 2];
        sprintf(username, "%s\n", argv[2]);
        write(sock, username, strlen(username));
      }
      else
      {
        int len = strlen(argv[1]);
        char username[len + 2];
        sprintf(username, "%s\n", argv[1]);
        write(sock, username, strlen(username));
      }

      if ((nbytes = read(sock, rbuf, 20)) < 0)
      {
        perror("read");
      }
      else
      {
        if (strncmp(rbuf, name_register, 20) == 0)
        {
          printf("user name registered\n");
          state = 4;
        }
        else
          state = 6;
      }
      break;

    case 4:
      if (flag == 1)
      {
        flag = 0;
        printf("Input menu number\n");
        printf("1 : username list\n");
        printf("2 : secret message\n");
        printf("the others : cancel\n");
        char menu_num[10];
        fgets(menu_num, 10, stdin);
        if (strcmp(menu_num, "1\n") == 0)
        {
          write(sock, req_list, strlen(req_list));
        }
        else if (strcmp(menu_num, "2\n") == 0)
        {
          char username[128];
          char message[1024 - 128 - strlen(seqret_key)];
          printf("Input username who you send message\n");
          if (fgets(username, 128, stdin) == NULL || username[0] == '\n')
          {
            printf("Invalid username\n");
          }
          else
          {
            printf("Input message\n");
            if (fgets(message, 1024 - 128 - strlen(seqret_key), stdin) == NULL || message[0] == '\n')
            {
              printf("Invalid username\n");
            }
            else
            {
              char *p;
              p = strchr(username, '\n');
              if (p != NULL)
              {
                *p = '\0';
              }
              sprintf(sbuf, "%s %s %s", seqret_key, username, message);
              write(sock, sbuf, strlen(sbuf));
            }
          }
        }
        printf("menu finished\n");
        state = 4;
        break;
      }
      /* 状態4処理 */
      FD_ZERO(&rfds);   /* rfds を空集合に初期化 */
      FD_SET(0, &rfds); /* 標準入力 */
      FD_SET(sock, &rfds);
      /* 監視する待ち時間を 1 秒に設定 */
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      /* 標準入力とソケットからの受信を同時に監視する */
      if (select(sock + 1, &rfds, NULL, NULL, &tv) > 0)
      {
        if (FD_ISSET(0, &rfds))
        {
          if ((nbytes = read(0, sbuf, sizeof(sbuf))) < 0)
          {
            perror("read");
          }
          else if (nbytes == 0)
          {
            state = 5;
            break;
          }
          else
          {
            write(sock, sbuf, nbytes);
          }
        }
        if (FD_ISSET(sock, &rfds))
        { /* ソケットから受信したなら */
          /* ソケットから読み込み端末に出力 */
          if ((nbytes = read(sock, rbuf, sizeof(rbuf))) < 0)
          {
            perror("read");
          }
          else if (nbytes == 0)
          {
            state = 5;
            break;
          }
          else
          {
            write(1, rbuf, nbytes); /*受信文字列をそのまま表示 */
          }
        }
      }
      state = 4;
      break;

    case 5:
      close(sock);
      exit(0);
      break;

    case 6:
      write(1, rbuf, nbytes);
      close(sock);
      exit(1);

      break;
    }
  } while (1); /* 繰り返す */
}

