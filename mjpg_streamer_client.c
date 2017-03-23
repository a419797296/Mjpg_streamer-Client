#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>   

#define SERVER_PORT         8080
#define V4L2_PIX_FMT_MJPEG  1
#define BUFFER_SIZE         1024
int iSocketClient;

/* ͼƬ���������� */
typedef struct PixelDatas {
  int iWidth;   /* ���: һ���ж��ٸ����� */
  int iHeight;  /* �߶�: һ���ж��ٸ����� */
  int iBpp;     /* һ�������ö���λ����ʾ */
  int iLineBytes;  /* һ�������ж����ֽ� */
  int iTotalBytes; /* �����ֽ��� */ 
  unsigned char *aucPixelDatas;  /* �������ݴ洢�ĵط� */
}T_PixelDatas, *PT_PixelDatas;

typedef struct VideoBuf {
  T_PixelDatas tPixelDatas;
  int iPixelFormat;
  /* signal fresh frames */
  pthread_mutex_t db;
  pthread_cond_t  db_update;
}T_VideoBuf, *PT_VideoBuf;


typedef struct VideoRecv {
  char *name;
  
  int (*Connect_To_Server)(int *SocketClient, const char *ip);
  int (*DisConnect_To_Server)(int *SocketClient);
  int (*Init)(int *SocketClient);
  int (*GetFormat)(void);
  int (*Get_Video)(int *SocketClient, PT_VideoBuf ptVideoBuf);
  struct VideoRecv *ptNext;
}T_VideoRecv, *PT_VideoRecv;


static int connect_to_server(int *SocketClient, const char *ip);
static int disconnect_to_server(int *SocketClient);
static int init(int *SocketClient);  /* ��һЩ��ʼ������ */
static int getformat(void);
static long int getFileLen(int *SocketClient, char *FreeBuf, int *FreeLen);
static long int http_recv(int *SocketClient, char **lpbuff, long int size);
static int get_video(int *SocketClient, PT_VideoBuf ptVideoBuf);
static void *RecvVideoThread(void *tVideoBuf);


static int connect_to_server(int *SocketClient, const char *ip)
{
  int iRet;
  struct sockaddr_in tSocketServerAddr;
  
  *SocketClient = socket(AF_INET, SOCK_STREAM, 0);
  
  tSocketServerAddr.sin_family      = AF_INET;
  tSocketServerAddr.sin_port        = htons(SERVER_PORT);  /* host to net, short */
  //tSocketServerAddr.sin_addr.s_addr = INADDR_ANY;
  if (0 == inet_aton(ip, &tSocketServerAddr.sin_addr))
  {
    printf("invalid server_ip\n");
    return -1;
  }
  memset(tSocketServerAddr.sin_zero, 0, 8);
  
  iRet = connect(*SocketClient, (const struct sockaddr *)&tSocketServerAddr, sizeof(struct sockaddr));
  if (-1 == iRet)
  {
    printf("connect error!\n");
    return -1;
  }
  
  return 0;
}

static int disconnect_to_server(int *SocketClient)
{
  close(*SocketClient);
  return 0;
}


static int init(int *SocketClient)  /* ��һЩ��ʼ������ */
{
  char ucSendBuf[100];
  int iSendLen;
  
  int iRecvLen;
  unsigned char ucRecvBuf[1000];
  
  /* �����������ַ��� */
  memset(ucSendBuf, 0x0, 100);
  strcpy(ucSendBuf, "GET /?action=stream\n");   /* ����������ѡ�� action=stream*/
  iSendLen = send(*SocketClient, ucSendBuf, strlen(ucSendBuf), 0);
  if (iSendLen <= 0)
  {
    close(*SocketClient);
    return -1;
  }
  
  /* ������ǲ�ʹ�����빦��!��ֻ�跢�����ⳤ��ΪС��2�ֽڵ��ַ��� */
  memset(ucSendBuf, 0x0, 100);
  strcpy(ucSendBuf, "f\n");   /* �������ǲ�ѡ�����빦�� */
  iSendLen = send(*SocketClient, ucSendBuf, strlen(ucSendBuf), 0);
  if (iSendLen <= 0)
  {
    close(*SocketClient);
    return -1;
  }
  
  /* ���ӷ������˽���һ�α��� */
  /* ���տͻ��˷��������ݲ���ʾ���� */
  iRecvLen = recv(*SocketClient, ucRecvBuf, 999, 0);
  if (iRecvLen <= 0)
  {
    close(*SocketClient);
    return -1;
  }
  else
  {
    ucRecvBuf[iRecvLen] = '\0';
    printf("http header: %s\n", ucRecvBuf);
  }
  
  return 0;
}



static int getformat(void)
{
  /* ֱ�ӷ�����Ƶ�ĸ�ʽ */
  return V4L2_PIX_FMT_MJPEG;
}


static long int getFileLen(int *SocketClient, char *FreeBuf, int *FreeLen)
{
  int iRecvLen;
  long int videolen;
  char ucRecvBuf[1024];
  char *plen, *buffp;
  
  while(1)
  {
    iRecvLen = recv(*SocketClient, ucRecvBuf, 1024, 0); /* ��ȡ1024�ֽ����� */
    if (iRecvLen <= 0)
    {
      close(*SocketClient);
      return -1;
    }
    /* sprintf(buffer, "Content-Type: image/jpeg\r\n" \
      *               "Content-Length: %d\r\n" \
        *               "\r\n", frame_size);
    *����˻ᷢ��һЩ���ݱ�ͷ�����ǻ����ĸ�ʽ
    *
    */
    /* ����ucRecvBuf���жϽ��յ��������Ƿ��Ǳ��� */
    plen = strstr(ucRecvBuf, "Length:");  /* plenָ��ָ��Length�ַ�����ͷ�ĵ�ַ */
    if(NULL != plen)
    {
      plen = strchr(plen, ':');  /* ��plen�и��ҵ������ĵ�ַ */
      plen++;                   /* ָ����һ����ַ������ĵ�ַ�洢������Ƶ���ݵ�������С */
      videolen = atol(plen);    /* ��ȡ��Ƶ���ݵĴ�С */
      printf("the Video Len %ld\n", videolen);
    }
    
    buffp = strstr(ucRecvBuf, "\r\n\r\n");   /* \r\n\r\n����������whileѭ�� */
    if(buffp != NULL)
      break;
  }   
  
  buffp += 4;   /* ָ���������ݵĿ�ͷ�������ַ��1024�ֽ��� */
  /* ��Ҫע����������Ѿ�������1024�ֽڵ����ݣ�������1024�ֽڳ��˱�ͷ�󻹰�����һ��������
  *����������Ҫ����һ�£������߼���ϵһ��Ҫ�����ס�
  */
  *FreeLen = 1024 - (buffp - ucRecvBuf);  /* 1024�ֽ���ʣ�����Ƶ���� */
  memcpy(FreeBuf, buffp, *FreeLen);     /* ���ⲿ�ֵ���Ƶ���ݷ���FreeBuf�������� */
  return videolen;     /* ����1024�ֽ�����������Ƶ���ݵĴ�С */
}

/* �������������ȡ��1024�ֽڣ��������ݱ�ͷ����Ƶ���ݣ������������Ƶ���� */
static long int http_recv(int *SocketClient, char **lpbuff, long int size)
{    /* *lpbuff�������ʣ�����ݵĴ�С��sizeʣ����Ƶ���ݵĴ�С */
  int iRecvLen = 0, RecvLen = 0;
  char ucRecvBuf[BUFFER_SIZE];
  
  while(size > 0) /* ����һ�δ����ʣ�����ݽ����꣬ */
  {
    iRecvLen = recv(*SocketClient, ucRecvBuf, (size > BUFFER_SIZE)? BUFFER_SIZE: size, 0);
    if (iRecvLen <= 0)
      break;
    
    
    RecvLen += iRecvLen;
    size -= iRecvLen;
    
    
    if(*lpbuff == NULL)
    {
      *lpbuff = (char *)malloc(RecvLen);
      if(*lpbuff == NULL)
        return -1;
    }
    else
    {
      *lpbuff = (char *)realloc(*lpbuff, RecvLen);
      if(*lpbuff == NULL)
        return -1;
    }
    
    memcpy(*lpbuff+RecvLen-iRecvLen, ucRecvBuf, iRecvLen);
  }
  return RecvLen;    /* ������ν��յ������� */
}


/* �����������ȡ��Ƶ�������ݵĺ��� */
static int get_video(int *SocketClient, PT_VideoBuf ptVideoBuf)
{
  long int video_len, iRecvLen;
  int FirstLen = 0;
  char tmpbuf[1024];
  char *FreeBuffer = NULL;
  
  while(1)   /* �������ݴ����ptVideoBuf->tPixelDatas.aucPixelDatas�������� */
  { 
    /* ����1024�ֽ��е���Ƶ���� */
    video_len = getFileLen(SocketClient, tmpbuf, &FirstLen);
    /* ����ʣ����Ƶ���� */
    iRecvLen = http_recv(SocketClient, &FreeBuffer, video_len - FirstLen);
    printf("video_len = %ld,iRecvLen = %ld\n", video_len,iRecvLen);
    pthread_mutex_lock(&ptVideoBuf->db);
    
    /* �����ν��յ�����Ƶ������װ��һ֡���� */
    memcpy(ptVideoBuf->tPixelDatas.aucPixelDatas, tmpbuf, FirstLen);
    memcpy(ptVideoBuf->tPixelDatas.aucPixelDatas+FirstLen, FreeBuffer, iRecvLen);
    ptVideoBuf->tPixelDatas.iTotalBytes = video_len;

    pthread_cond_broadcast(&ptVideoBuf->db_update);// ����һ�����ݸ��µ��źţ�֪ͨ����ͨ����ȡ����
    pthread_mutex_unlock( &ptVideoBuf->db );// ԭ�Ӳ�������
  }
  return 0;
}


/* ���� */
static T_VideoRecv g_tVideoRecv = {
  .name        = "http",      /* �ṹ���Ա�� */
  .Connect_To_Server  = connect_to_server,     /* ���ӵ��������� */
  .DisConnect_To_Server     = disconnect_to_server,    /* ɾ������ */
  .Init = init,                      /* ����ʼ������ */
  .GetFormat= getformat,                 /* �õ�����ͷ���ݸ�ʽ */
  .Get_Video= get_video,                 /* ��ȡ��Ƶ���� */
};


static void *RecvVideoThread(void *tVideoBuf)
{
  g_tVideoRecv.Get_Video(&iSocketClient, (PT_VideoBuf)tVideoBuf);
  return ((void *)0);
}

int main(int argc,char *argv[])
{

  pthread_t RecvVideo_Id;
  PT_VideoBuf ptVideoBuf;
  T_VideoBuf tVideoBuf;
  int handle,res,fileNums=0;
  char fileNames[40]={0};
  time_t nowtime;
  struct tm *timeinfo;
  char *snap_shot_name;

  if (argc != 2)
  {
  printf("Usage:\n");
  printf("%s <ip>\n", argv[0]);
  return -1;
  }
  /* ��������ͷ�豸 */ 
  if(g_tVideoRecv.Connect_To_Server(&iSocketClient, argv[1]) < 0)
  { 
    printf("can not Connect_To_Server\n");
    return -1;
  }

  if(g_tVideoRecv.Init(&iSocketClient) < 0)
  {
    printf("can not Init\n");
    return -1;
  }
  //��video_buf��0�����ڻ�ȡһ֡����
  memset(&tVideoBuf, 0, sizeof(tVideoBuf));
// ���仺�棨30000�ֽڣ�
  tVideoBuf.tPixelDatas.aucPixelDatas = malloc(100000);

  if( pthread_mutex_init(&tVideoBuf.db, NULL) != 0 )
  /* ��ʼ�� global.db ��Ա */
  {
    return -1;
  }
  if( pthread_cond_init(&tVideoBuf.db_update, NULL) != 0 )
  /* ��ʼ�� global.db_update(��������) ��Ա */
  {
    printf("could not initialize condition variable\n");
    return -1;
  }

  /* ������ȡ����ͷ���ݵ��߳� */
  pthread_create(&RecvVideo_Id, NULL, &RecvVideoThread, &tVideoBuf);

  if(access("video_snapshot",0)==-1)//access�����ǲ鿴�ļ��ǲ��Ǵ���
  {
      if (mkdir("video_snapshot",0777))//��������ھ���mkdir����������
      {
          printf("creat file bag failed!!!");
          exit(1);
      }
  }
  while(1)
  {

    pthread_cond_wait(&tVideoBuf.db_update, &tVideoBuf.db);
    while(fileNums++<100)
    {
      time(&nowtime);
      timeinfo=localtime(&nowtime);
      //snap_shot_name=strdup(asctime(timeinfo));//��佫���ת��Ϊ�ַ���
      // sprintf(fileNames,"video_snapshot/%d%d%d_%d:%d:%d_%d.jpg",(1900+timeinfo->tm_year),(timeinfo->tm_mon),(timeinfo->tm_mday),(timeinfo->tm_hour),(timeinfo->tm_min),(timeinfo->tm_sec),fileNums);
      // strftime(fileNames, 128, "video_snapshot/%F_%T", timeinfo);
      sprintf(fileNames+strftime(fileNames, 128, "video_snapshot/%F_%T", timeinfo),"_%d.jpg",fileNums);
      printf("%s\n",fileNames);

      if((handle=open(fileNames,O_WRONLY | O_CREAT, 0666))==-1)
      {
        printf("Error opening file.\n");
        exit(1);
      }
      // tVideoBuf.tPixelDatas.aucPixelDatas
      if((res=write(handle,tVideoBuf.tPixelDatas.aucPixelDatas,tVideoBuf.tPixelDatas.iTotalBytes))==-1)
      {
        printf("Error writing to the file.\n");
        printf("%d\n", res);
        exit(1);
      }

      // free(snap_shot_name);
      close(handle);   
      pthread_cond_wait(&tVideoBuf.db_update, &tVideoBuf.db);   

    }



  }

  pthread_detach(RecvVideo_Id);
// �ȴ��߳̽���,�Ա����������Դ
  return 0;
  
}
