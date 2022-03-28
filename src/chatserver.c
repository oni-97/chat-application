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

#define PORT 10140
#define MAXCLIENTS 5
#define TIMEOUT 60
volatile sig_atomic_t flag = 0;
int pid_list[MAXCLIENTS];
int order[MAXCLIENTS];

void setTimer(int clt_num, int pid)
{
  int i;
  int max = order[clt_num];
  for (i = 0; i < MAXCLIENTS; i++)
  {
    if (max < order[i])
      max = order[i];
    if (order[clt_num] != 0 && order[i] > order[clt_num])
      order[i]--;
  }
  if (order[clt_num] == 0)
    order[clt_num] = max + 1;
  else
    order[clt_num] = max;
  pid_list[clt_num] = pid;
}

void deleteClient(int clt_num)
{
  int i;
  if (kill(pid_list[clt_num], SIGTERM) == -1)
  {
    perror("kill failed.");
    exit(1);
  }
  for (i = 0; i < MAXCLIENTS; i++)
  {
    if (order[i] > order[clt_num])
      order[i]--;
  }
  pid_list[clt_num] = 0;
}

void myalarm(int clt_num)
{
  int pid;
  if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
  {
    perror("signal failed.");
    exit(1);
  }

  if (pid_list[clt_num] != 0)
  {
    if (kill(pid_list[clt_num], SIGTERM) == -1)
    {
      perror("kill failed.");
      exit(1);
    }
  }

  if ((pid = fork()) == -1)
  {
    perror("fork failed.");
    exit(1);
  }

  if (pid == 0)
  { /* Child process */
    sleep(TIMEOUT);
    if (kill(getppid(), SIGALRM) == -1)
    {
      perror("kill failed.");
      exit(1);
    }
    exit(0);
  }
  setTimer(clt_num, pid);
}

void timeout()
{
  flag = 1;
}

int timeoutUserNumber()
{
  flag = 0;
  int clt_num;
  int i;
  for (i = 0; i < MAXCLIENTS; i++)
  {
    if (order[i] == 1)
      clt_num = i;
    if (0 < order[i])
      order[i]--;
  }
  pid_list[clt_num] = 0;
  return clt_num;
}

int main(int argc, char **argv)
{
  int sock, csock[MAXCLIENTS + 1], sock_udp;
  struct sockaddr_in svr;
  struct sockaddr_in svr_udp;
  struct sockaddr_in clt;
  struct sockaddr_in sender;
  int clen, nbytes, reuse, state, clt_num, sender_len;
  char rbuf[1024], sbuf[1256], nbuf[128];
  char clt_name[MAXCLIENTS][128] = {'\0'}, clt_ip[MAXCLIENTS][17] = {'\0'};
  char req_accept[] = "REQUEST ACCEPTED\n";
  char req_reject[] = "REQUEST REJECTED\n";
  char name_register[] = "USERNAME REGISTERED\n";
  char name_reject[] = "USRNAME REJECTED\n";
  char long_name[] = "Too long user name. The maximam length is 127.\n";
  char invalid_name[] = "Invalid username.\n";
  char req_list[] = "/list\n";
  char seqret_key[] = "/send";
  char send_usage[] = "failed to send message.\nUsage : /send username message\n";
  char req_udp[] = "CONNECT REQUEST";
  char accept_udp[] = "CONNECT ACCEPTED";
  char name_use[] = "username already in use\n";
  fd_set rfds;
  struct timeval tv;
  time_t timer;
  struct tm *local;
  char send_time[5];

  if (argc > 1)
  {
    fprintf(stderr, "Usage: %s\n", argv[0]);
    exit(1);
  }

  /*初期化*/
  int i;
  for (i = 0; i < MAXCLIENTS + 1; i++)
  {
    csock[i] = -1;
    pid_list[i] = 0;
    order[i] = 0;
  }

  /**状態を1で初期化*/
  state = 1;

  if (signal(SIGALRM, timeout) == SIG_ERR)
  {
    perror("signal failed.");
    exit(1);
  }

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
      /*ソケットアドレス再利用の指定*/
      reuse = 1;
      if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
      {
        perror("setsockopt");
        exit(1);
      }
      /* client受付用ソケットの情報設定*/
      bzero(&svr, sizeof(svr));
      svr.sin_family = AF_INET;
      svr.sin_addr.s_addr = htonl(INADDR_ANY); /*受付側のIPアドレスは任意*/
      svr.sin_port = htons(PORT);              /*ポート番号10140を介して受け付ける*/
      /*ソケットにソケットアドレスを割り当てる*/
      if (bind(sock, (struct sockaddr *)&svr, sizeof(svr)) < 0)
      {
        perror("bind");
        exit(1);
      }
      /*待ち受けクライアント数の設定*/
      if (listen(sock, 5) < 0)
      { /*待ち受け数に5を指定*/
        perror("listen");
        exit(1);
      }

      /*udpのソケット*/
      if ((sock_udp = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
      {
        perror("socket");
        exit(1);
      }

      if (bind(sock_udp, (struct sockaddr *)&svr, sizeof(svr)) < 0)
      {
        perror("bind");
        exit(1);
      }

      //受け入れるパケットの送信元を指定(0.0.0.0で任意のアドレスからのパケットを受信)
      bzero(&sender, sizeof(sender));
      sender_len = sizeof(sender);

      state = 2;

      break;

    case 2:
      /* 状態2処理 */
      if (flag == 1)
      {
        clt_num = timeoutUserNumber();
        state = 8;
        break;
      }
      FD_ZERO(&rfds);   /* rfds を空集合に初期化 */
      FD_SET(0, &rfds); /* 標準入力 */
      FD_SET(sock, &rfds);
      FD_SET(sock_udp, &rfds);
      int j;
      for (j = 0; j < MAXCLIENTS; j++)
      {
        if (csock[j] != -1)
          FD_SET(csock[j], &rfds); /* クライアントを受け付けたソケット */
      }
      /* 監視する待ち時間を 1 秒に設定 */
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      /* 標準入力とソケットからの受信を同時に監視する */
      int k;
      int max = sock;
      if (max < sock_udp)
        max = sock_udp;
      for (k = 0; k < MAXCLIENTS; k++)
      {
        if (max < csock[k])
          max = csock[k]; /* クライアントを受け付けたソケット */
      }
      if (select(max + 1, &rfds, NULL, NULL, &tv) > 0)
        state = 3;
      else
        state = 2;

      break;

    case 3:
      /* 状態3処理 */
      if (FD_ISSET(sock, &rfds))
      {
        state = 4;
        FD_CLR(sock, &rfds);
      }
      else if (FD_ISSET(sock_udp, &rfds))
      {
        if ((nbytes = recvfrom(sock_udp, rbuf, sizeof(rbuf), 0,
                               (struct sockaddr *)&sender, &sender_len)) < 0)
        {
          perror("recvfrom");
          exit(1);
        }
        if (strcmp(rbuf, req_udp) == 0)
        {
          sendto(sock_udp, accept_udp, strlen(accept_udp), 0, (struct sockaddr *)&sender, sizeof(sender));
        }
        state = 4;
        FD_CLR(sock_udp, &rfds);
        break;
      }
      else
      {
        clt_num = 0;
        int l;
        for (l = 0; l < MAXCLIENTS; l++)
        {
          if (FD_ISSET(csock[l], &rfds))
          {
            clt_num = l;
            state = 6;
            FD_CLR(csock[l], &rfds);
            break;
          }
        }
        if (l == MAXCLIENTS)
          state = 2;
      }

      break;

    case 4:
      /* 状態4処理 */
      clt_num = 0;
      while (csock[clt_num] != -1)
        clt_num++;

      clen = sizeof(clt);
      if ((csock[clt_num] = accept(sock, (struct sockaddr *)&clt, &clen)) < 0)
      {
        perror("accept");
        exit(2);
      }

      if (clt_num < MAXCLIENTS)
      {
        write(csock[clt_num], req_accept, strlen(req_accept));
        state = 5;
      }
      else
      {
        write(csock[clt_num], req_reject, strlen(req_reject));
        csock[clt_num] = -1;
        close(csock[clt_num]);
        state = 3;
      }

      break;

    case 5:
      /* 状態5処理 */
      /*受信*/
      if ((nbytes = read(csock[clt_num], rbuf, sizeof(rbuf))) < 0)
      {
        perror("read");
      }
      strncpy(nbuf, rbuf, 128);
      /*改行変換*/
      char *p;
      p = strchr(nbuf, '\n');
      if (p != NULL)
      {
        *p = '\0';
      }
      else
      { /*長すぎる名前*/
        nbuf[127] = '\0';
        printf("Too long username. The maximam length is 127. The overflowed part is not used.\n");
      }

      /*名前確認*/
      int r = 0;
      while (nbuf[r] != '\0')
      {
        if (isalnum(nbuf[r]) || nbuf[r] == '-' || nbuf[r] == '_')
          r++;
        else
          break;
      }
      if (nbuf[r] != '\0')
      {
        write(csock[clt_num], name_reject, strlen(name_reject));
        printf("connection failed. Invaid username:%s\n", nbuf);
        csock[clt_num] = -1;
        close(csock[clt_num]);
        state = 3;
        break;
      }

      int n;
      for (n = 0; n < MAXCLIENTS; n++)
      {
        if (csock[n] != -1 && strcmp(nbuf, clt_name[n]) == 0)
        {
          /*登録拒否*/
          write(csock[clt_num], name_reject, strlen(name_reject));
          printf("connection failed. username already in use:%s\n", nbuf);
          csock[clt_num] = -1;
          close(csock[clt_num]);
          state = 3;
          break;
        }
      }

      /*登録*/
      if (n == MAXCLIENTS)
      {
        strcpy(clt_name[clt_num], nbuf);
        write(csock[clt_num], name_register, strlen(name_register));
        printf("usr<%s> conected\n", nbuf);
        myalarm(clt_num);
        int people = 0;
        int z;
        for (z = 0; z < MAXCLIENTS; z++)
        {
          if (csock[z] != -1)
            people++;
        }
        sprintf(sbuf, "%s joined! %d people\n", clt_name[clt_num], people);
        for (z = 0; z < MAXCLIENTS; z++)
        {
          if (csock[z] != -1)
          {
            write(csock[z], sbuf, strlen(sbuf));
          }
        }
        strcpy(clt_ip[clt_num], inet_ntoa(clt.sin_addr));
        state = 3;
      }

      break;

    case 6:
      myalarm(clt_num);
      /* 状態6処理 メッセージ配信 */
      if ((nbytes = read(csock[clt_num], rbuf, sizeof(rbuf))) < 0)
      {
        perror("read");
      }
      else if (nbytes == 0)
      {
        state = 7;
        break;
      }
      else
      {
        rbuf[nbytes] = '\0'; //\0を挿入
        /*時刻*/
        timer = time(NULL);        /* 現在時刻を取得 */
        local = localtime(&timer); /* 地方時に変換 */
        sprintf(send_time, "%d:%d", local->tm_hour, local->tm_min);

        if (strcmp(rbuf, req_list) == 0)
        { /*リスト送信*/
          char name_list[128 * MAXCLIENTS];
          name_list[0] = '\0';
          int q;
          for (q = 0; q < MAXCLIENTS; q++)
          {
            if (csock[q] != -1)
            {
              if (name_list[0] == '\0')
                sprintf(name_list, "%s", clt_name[q]);
              else
                sprintf(name_list, "%s %s", name_list, clt_name[q]);
            }
          }
          sprintf(sbuf, "%s username list\n%s\n", send_time, name_list);
          write(csock[clt_num], sbuf, strlen(sbuf));
        }
        else
        {
          char cpy[1024];
          strcpy(cpy, rbuf);
          char *ptr;
          ptr = strtok(cpy, " ");
          if (strcmp(ptr, seqret_key) == 0)
          {
            char *mes;
            if ((ptr = strtok(NULL, " ")) == NULL || (mes = strtok(NULL, "")) == NULL)
            {
              write(csock[clt_num], send_usage, strlen(send_usage));
            }
            else
            {
              /*改行変換*/
              char *p;
              p = strchr(mes, '\n');
              if (p != NULL)
              {
                *p = '\0';
              }
              sprintf(sbuf, "%s [%s] %s> %s >%s\n", send_time, clt_ip[clt_num], clt_name[clt_num], mes, ptr);
              int s;
              for (s = 0; s < MAXCLIENTS; s++)
              {
                if (csock[s] != -1 && strcmp(ptr, clt_name[s]) == 0)
                {
                  write(csock[s], sbuf, strlen(sbuf));
                  if (s != clt_num)
                    write(csock[clt_num], sbuf, strlen(sbuf));
                  break;
                }
              }
              if (s == MAXCLIENTS)
              {
                sprintf(sbuf, "failed to send message. username <%s> not found.\n", ptr);
                write(csock[clt_num], sbuf, strlen(sbuf));
              }
            }
          }
          else
          {
            if (strcmp(rbuf, "/send\n") == 0)
            {
              write(csock[clt_num], send_usage, strlen(send_usage));
              state = 3;
              break;
            }

            sprintf(sbuf, "%s [%s] %s> %s", send_time, clt_ip[clt_num], clt_name[clt_num], rbuf);

            /*送信*/
            int p;
            for (p = 0; p < MAXCLIENTS; p++)
            {
              if (csock[p] != -1)
              {
                write(csock[p], sbuf, strlen(sbuf));
              }
            }
          }
        }
        state = 3;
      }

      break;

    case 7:
      close(csock[clt_num]);
      printf("usr<%s> closed\n", clt_name[clt_num]);
      csock[clt_num] = -1;
      deleteClient(clt_num);
      int people = 0;
      int z;
      for (z = 0; z < MAXCLIENTS; z++)
      {
        if (csock[z] != -1)
          people++;
      }
      sprintf(sbuf, "%s left. %d people\n", clt_name[clt_num], people);
      for (z = 0; z < MAXCLIENTS; z++)
      {
        if (csock[z] != -1)
        {
          write(csock[z], sbuf, strlen(sbuf));
        }
      }
      clt_name[clt_num][0] = '\0';
      clt_ip[clt_num][0] = '\0';
      state = 3;
      break;

    case 8:
      write(csock[clt_num], "timeout\n", strlen("timeout\n"));
      close(csock[clt_num]);
      printf("usr<%s> closed\n", clt_name[clt_num]);
      csock[clt_num] = -1;
      people = 0;
      for (z = 0; z < MAXCLIENTS; z++)
      {
        if (csock[z] != -1)
          people++;
      }
      sprintf(sbuf, "%s left. %d people\n", clt_name[clt_num], people);
      for (z = 0; z < MAXCLIENTS; z++)
      {
        if (csock[z] != -1)
        {
          write(csock[z], sbuf, strlen(sbuf));
        }
      }
      clt_name[clt_num][0] = '\0';
      clt_ip[clt_num][0] = '\0';
      state = 2;
      break;
    }
  } while (1); /* 繰り返す */
  close(sock_udp);
}
