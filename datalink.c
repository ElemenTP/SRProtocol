/*************************滑动窗口协议实验 选择重传v1.0 20210508*************************/
#include "datalink.h"
#include "protocol.h"
#include <stdio.h>
#include <string.h>
//0-31循环
#define incre(k)     \
    if (k < MAX_SEQ) \
        k++;         \
    else             \
        k = 0
//重传超时时间
#define DATA_TIMER 1800
//ACK捎带超时
#define ACK_TIMER 300
//最大序号
#define MAX_SEQ 31
//窗口大小
#define NR_BUFS 16
//定义布尔类型
typedef enum
{
    false,
    true
} bool;
//定义缓存状态类型
typedef enum
{
    empty,
    used,
    spec
} status;
//方便使用一字节数据
typedef unsigned char byte;
//帧数据结构，实际上只是定义了一个263字节大小的区域，与特定位置的指针
typedef struct
{
    byte kind;            //帧类型
    byte seq;             //数据帧序号，ACK帧、NAK帧的序号
    byte ack;             //数据帧捎带ACK序号，不捎带时为数据指针
    byte data[PKT_LEN];   //数据帧不捎带ACK时的数据指针
    unsigned int padding; //用于CRC的填充
} FRAME;
//缓存数据结构
typedef struct
{
    byte data[PKT_LEN]; //缓存数据
    status buf_status;  //缓存状态
    int data_length;    //数据长度
} BUFFER;
//物理层是否可用的全局变量，由物理层控制
static int phl_ready = false;
//将帧加入CRC校验位，并传递给物理层
static void put_frame(unsigned char *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = false;
}
//发送数据帧
static void send_data_frame(byte seq, byte ack, byte *data, int data_length)
{
    FRAME s;

    s.seq = seq;
    //判断是否需要捎带ACK
    if (ack > MAX_SEQ)
    {
        s.kind = FRAME_DATA;
        memcpy(&(s.ack), data, data_length);

        dbg_frame("Send DATA %d, ID %d\n", seq, *(short *)&(s.ack));

        put_frame((unsigned char *)&s, 2 + data_length);
    }
    else
    {
        s.kind = FRAME_DATA_ACK;
        s.ack = ack;
        memcpy(s.data, data, data_length);

        dbg_frame("Send DATA with ACK %d %d, ID %d\n", seq, ack, *(short *)s.data);

        put_frame((unsigned char *)&s, 3 + data_length);
        stop_ack_timer();
    }

    start_timer(seq, DATA_TIMER);
}
//发送ACK帧
static void send_ack_frame(byte ack)
{
    FRAME s;

    s.kind = FRAME_ACK;
    s.seq = ack;

    dbg_frame("Send ACK %d\n", ack);
    put_frame((unsigned char *)&s, 2);
}
//发送NAK帧
static void send_nak_frame(byte nak)
{
    FRAME s;

    s.kind = FRAME_NAK;
    s.seq = nak;

    dbg_frame("Send NAK %d\n", nak);
    put_frame((unsigned char *)&s, 2);
}
//判断某个序号是否在窗口中
static bool inwindow(int lower, int k, int upper)
{
    switch (((lower < upper) << 2) + ((k < lower) << 1) + (k > upper))
    {
    case 1:
        return true;

    case 2:
        return true;

    case 4:
        return true;

    default:
        return false;
    }
}

int main(int argc, char **argv)
{
    byte send_upper, send_lower; //发送窗口上界，下界
    byte to_send;                //下一个发送序号
    byte recv_upper, recv_lower; //接收窗口上界，下届
    byte to_ack;                 //需要发送的ACK
    byte ptr_tmp;                //临时缓存指针
    int event, arg, length;      //事件编号，事件的参数，接收帧长度
    BUFFER send_buf[NR_BUFS];    //发送缓存组
    BUFFER recv_buf[NR_BUFS];    //接收缓存组
    FRAME recv_tmp;              //接收帧缓存

    send_lower = 0;
    send_upper = NR_BUFS - 1;
    to_send = 0;
    recv_lower = 0;
    recv_upper = NR_BUFS - 1;
    to_ack = MAX_SEQ + 1; //不需要发送ACK时to_ack为违例编号
    memset(send_buf, 0, NR_BUFS * sizeof(BUFFER));
    memset(recv_buf, 0, NR_BUFS * sizeof(BUFFER));

    protocol_init(argc, argv);
    lprintf("Designed by Zhang JY, Xu RJ, Hu YM, build: " __DATE__ "  "__TIME__
            "\n");

    disable_network_layer();

    while (true)
    {
        //接收事件编号与参数
        event = wait_for_event(&arg);

        switch (event)
        {
            //网络层要发包
        case NETWORK_LAYER_READY:
            //将包存入缓存，并得到数据长度
            send_buf[to_send % NR_BUFS].data_length = get_packet(send_buf[to_send % NR_BUFS].data);
            //将缓存标记为已用
            send_buf[to_send % NR_BUFS].buf_status = used;
            //将数据成帧并发送
            send_data_frame(to_send, to_ack, send_buf[to_send % NR_BUFS].data, send_buf[to_send % NR_BUFS].data_length);
            //发送数据帧时已发送待发ACK，将to_ack设为违例编号
            to_ack = MAX_SEQ + 1;
            //发送序号增加
            incre(to_send);
            break;

            //物理层可发送
        case PHYSICAL_LAYER_READY:
            phl_ready = true;
            break;

            //收到帧
        case FRAME_RECEIVED:
            //获取收到的帧和长度
            length = recv_frame((unsigned char *)&recv_tmp, sizeof(recv_tmp));
            //若帧长度不符合任何可能的帧，或者CRC校验错误，认为收到了错误的帧
            if (length < 6 || crc32((unsigned char *)&recv_tmp, length) != 0)
            {
                dbg_event("****** Receiver Error, Bad CRC ******\n");
                //若接收窗口下界所指帧未发送过NAK则发送NAK，并标记已发送过NAK
                if (recv_buf[recv_lower % NR_BUFS].buf_status != spec)
                {
                    send_nak_frame(recv_lower);
                    recv_buf[recv_lower % NR_BUFS].buf_status = spec;
                }
                break;
            }

            //收到正确的帧
            switch (recv_tmp.kind)
            {
                //收到数据帧
            case FRAME_DATA:
                //该数据帧编号在窗口中，且对应缓存未被占用
                if (inwindow(recv_lower, recv_tmp.seq, recv_upper) && recv_buf[recv_tmp.seq % NR_BUFS].buf_status != used)
                {
                    //将接收到的帧的数据，数据长度加入缓存，并标记该缓存已用
                    memcpy(recv_buf[recv_tmp.seq % NR_BUFS].data, &(recv_tmp.ack), length - 6);
                    recv_buf[recv_tmp.seq % NR_BUFS].data_length = length - 6;
                    recv_buf[recv_tmp.seq % NR_BUFS].buf_status = used;
                    //检测是否已有待发ACK，无则设置当前ACK为待发ACK，有则立刻发送待发ACK后设置当前ACK为待发ACK
                    if (to_ack > MAX_SEQ)
                        to_ack = recv_tmp.seq;
                    else
                    {
                        stop_ack_timer;
                        send_ack_frame(to_ack);
                        to_ack = recv_tmp.seq;
                    }
                    start_ack_timer(ACK_TIMER);
                    dbg_event("Receive DATA %d, ID %d\n", recv_tmp.seq, *(short *)recv_buf[recv_tmp.seq % NR_BUFS].data);
                    //检测丢帧，有丢帧则将疑似丢失且未发送过NAK的帧发送NAK
                    if (recv_tmp.seq != recv_lower)
                    {
                        dbg_event("Frames lost detected.\n");
                        ptr_tmp = recv_lower;
                        while (ptr_tmp != recv_tmp.seq)
                        {
                            if (recv_buf[ptr_tmp % NR_BUFS].buf_status == empty)
                            {
                                send_nak_frame(ptr_tmp);
                                recv_buf[ptr_tmp % NR_BUFS].buf_status = spec;
                            }
                            incre(ptr_tmp);
                        }
                    }
                }
                //该数据帧编号在窗口外，或者对应缓存已占用，则认为收到重复帧，重发该帧ACK
                else
                {
                    dbg_event("Repeated frame detected.\n");
                    if (to_ack > MAX_SEQ)
                        to_ack = recv_tmp.seq;
                    else
                    {
                        stop_ack_timer;
                        send_ack_frame(to_ack);
                        to_ack = recv_tmp.seq;
                    }
                    start_ack_timer(ACK_TIMER);
                }
                break;

                //收到捎带ACK的数据帧
            case FRAME_DATA_ACK:
                if (inwindow(recv_lower, recv_tmp.seq, recv_upper) && recv_buf[recv_tmp.seq % NR_BUFS].buf_status != used)
                {
                    memcpy(recv_buf[recv_tmp.seq % NR_BUFS].data, recv_tmp.data, length - 7);
                    recv_buf[recv_tmp.seq % NR_BUFS].data_length = length - 7;
                    recv_buf[recv_tmp.seq % NR_BUFS].buf_status = used;
                    if (to_ack > MAX_SEQ)
                        to_ack = recv_tmp.seq;
                    else
                    {
                        stop_ack_timer;
                        send_ack_frame(to_ack);
                        to_ack = recv_tmp.seq;
                    }
                    start_ack_timer(ACK_TIMER);
                    dbg_event("Receive DATA with ACK %d %d, ID %d\n", recv_tmp.seq, recv_tmp.ack, *(short *)recv_buf[recv_tmp.seq % NR_BUFS].data);
                    if (recv_tmp.seq != recv_lower)
                    {
                        dbg_event("Detect frames lost.\n");
                        ptr_tmp = recv_lower;
                        while (ptr_tmp != recv_tmp.seq)
                        {
                            if (recv_buf[ptr_tmp % NR_BUFS].buf_status == empty)
                            {
                                send_nak_frame(ptr_tmp);
                                recv_buf[ptr_tmp % NR_BUFS].buf_status = spec;
                            }
                            incre(ptr_tmp);
                        }
                    }
                }
                else
                {
                    dbg_event("Repeated frame detected.\n");
                    if (to_ack > MAX_SEQ)
                        to_ack = recv_tmp.seq;
                    else
                    {
                        stop_ack_timer;
                        send_ack_frame(to_ack);
                        to_ack = recv_tmp.seq;
                    }
                    start_ack_timer(ACK_TIMER);
                }
                //处理捎带ACK，ACK序号在窗口中且对应缓存已占用，则标记该缓存为已收到ACK的状态，停止重传计时
                if (inwindow(send_lower, recv_tmp.ack, send_upper) && send_buf[recv_tmp.ack % NR_BUFS].buf_status == used)
                {
                    send_buf[recv_tmp.ack % NR_BUFS].buf_status = spec;
                    stop_timer(recv_tmp.ack);
                }
                break;

                //收到ACK帧
            case FRAME_ACK:
                if (inwindow(send_lower, recv_tmp.seq, send_upper) && send_buf[recv_tmp.seq % NR_BUFS].buf_status == used)
                {
                    dbg_event("Receive ACK %d.\n", recv_tmp.seq);
                    send_buf[recv_tmp.seq % NR_BUFS].buf_status = spec;
                    stop_timer(recv_tmp.seq);
                }
                break;

                //收到NAK帧
            case FRAME_NAK:
                //NAK序号在窗口中且对应缓存已占用，则立刻重传该帧
                if (inwindow(send_lower, recv_tmp.seq, send_upper) && send_buf[recv_tmp.seq % NR_BUFS].buf_status == used)
                {
                    dbg_event("Receive NAK %d.\n", recv_tmp.seq);
                    stop_timer(recv_tmp.seq);
                    send_data_frame(recv_tmp.seq, to_ack, send_buf[recv_tmp.seq % NR_BUFS].data, send_buf[recv_tmp.seq % NR_BUFS].data_length);
                    to_ack = MAX_SEQ + 1;
                }
                break;

                //非任何已知帧类型，可能是CRC未检出错误，处理同错误帧
            default:
                dbg_event("****** Receiver Error, Bad CRC ******\n");
                if (recv_buf[recv_lower % NR_BUFS].buf_status != spec)
                {
                    send_nak_frame(recv_lower);
                    recv_buf[recv_lower % NR_BUFS].buf_status = spec;
                }
            }
            break;

            //ACK计时器超时，立刻发送待发ACK
        case ACK_TIMEOUT:
            dbg_event("******* ACK timeout *******\n");
            send_ack_frame(to_ack);
            to_ack = MAX_SEQ + 1;
            break;

            //数据计时器超时，立刻重发超时数据
        case DATA_TIMEOUT:
            dbg_event("******* DATA %d timeout *******\n", arg);
            send_data_frame(arg, to_ack, send_buf[arg % NR_BUFS].data, send_buf[arg % NR_BUFS].data_length);
            to_ack = MAX_SEQ + 1;
            break;
        }

        //debug用，记录窗口移动前的窗口下界
        int tmp_lower = recv_lower;

        //若接收窗口下界缓存状态为已用则将数据传递给网络层并移动窗口，循环尽可能移动窗口
        while (recv_buf[recv_lower % NR_BUFS].buf_status == used)
        {
            put_packet(recv_buf[recv_lower % NR_BUFS].data, recv_buf[recv_lower % NR_BUFS].data_length);
            recv_buf[recv_lower % NR_BUFS].buf_status = empty;
            incre(recv_lower);
            incre(recv_upper);
        }

        //检测到窗口移动就发送debug事件
        if (tmp_lower != recv_lower)
            dbg_event("Receiver window moved, new lower %d, new upper %d\n", recv_lower, recv_upper);

        tmp_lower = send_lower;

        //若发送窗口下界缓存状态为已收到ACK则移动窗口，循环尽可能移动窗口
        while (send_buf[send_lower % NR_BUFS].buf_status == spec)
        {
            send_buf[send_lower % NR_BUFS].buf_status = empty;
            incre(send_lower);
            incre(send_upper);
        }

        //检测到窗口移动就发送debug事件
        if (tmp_lower != send_lower)
            dbg_event("Sender window moved, new lower %d, new upper %d\n", send_lower, send_upper);

        //当发送序号在发送窗口中，且物理层可用时开启网络层
        if (inwindow(send_lower, to_send, send_upper) && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}