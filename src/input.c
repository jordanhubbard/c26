#include "c26.h"
#include "c26_input.h"
#include "c26_virtio.h"

#define VIRTIO_DEVICE_INPUT 18U
#define INPUT_DEVICE_LIMIT 3U

typedef struct {
    c26_virtio_device_t device;
    c26_virtq_t queue;
    c26_virtq_desc_t descriptors[C26_VIRTQ_SIZE];
    c26_virtq_avail_t available;
    c26_virtq_used_t used;
    c26_input_event_t events[C26_VIRTQ_SIZE];
    int online;
} input_device_t;

static input_device_t inputs[INPUT_DEVICE_LIMIT] __attribute__((aligned(4096)));
static unsigned int input_count;

static int initialize_one(unsigned int instance, input_device_t *input)
{
    if (!c26_virtio_find(VIRTIO_DEVICE_INPUT, instance, &input->device) ||
        !c26_virtio_begin(&input->device, 0, 0) ||
        !c26_virtio_queue_setup(&input->device, 0, &input->queue,
                                input->descriptors, &input->available,
                                &input->used, C26_VIRTQ_SIZE) ||
        !c26_virtio_finish(&input->device)) {
        return 0;
    }
    for (uint16_t i = 0; i < input->queue.size; i++) {
        input->events[i] = (c26_input_event_t){0, 0, 0};
        input->descriptors[i] = (c26_virtq_desc_t){
            (uint64_t)(uintptr_t)&input->events[i], sizeof(c26_input_event_t),
            C26_VIRTQ_DESC_WRITE, 0};
        c26_virtq_submit(&input->queue, i);
    }
    input->online = 1;
    return 1;
}

unsigned int c26_input_init(void)
{
    input_count = 0;
    for (unsigned int i = 0; i < INPUT_DEVICE_LIMIT; i++) {
        if (!initialize_one(i, &inputs[input_count])) {
            break;
        }
        input_count++;
    }
    c26_puts("VIRTIO INPUT: ");
    c26_put_uint(input_count);
    c26_puts(" device(s) online\n");
    return input_count;
}

int c26_input_poll(c26_input_event_t *event)
{
    for (unsigned int i = 0; i < input_count; i++) {
        input_device_t *input = &inputs[i];
        uint32_t id;
        if (!c26_virtq_pop(&input->queue, &id, 0)) {
            continue;
        }
        if (id >= input->queue.size) {
            continue;
        }
        c26_memory_barrier();
        *event = input->events[id];
        input->events[id] = (c26_input_event_t){0, 0, 0};
        c26_virtq_submit(&input->queue, (uint16_t)id);
        c26_virtio_ack_interrupt(&input->device);
        return 1;
    }
    return 0;
}

char c26_input_key_to_ascii(uint16_t code, int shift)
{
    static const char unshifted[] = {
        [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',
        [10]='9',[11]='0',[12]='-',[13]='=',[15]='\t',[16]='q',[17]='w',
        [18]='e',[19]='r',[20]='t',[21]='y',[22]='u',[23]='i',[24]='o',
        [25]='p',[26]='[',[27]=']',[30]='a',[31]='s',[32]='d',[33]='f',
        [34]='g',[35]='h',[36]='j',[37]='k',[38]='l',[39]=';',[40]='\'',
        [43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',
        [50]='m',[51]=',',[52]='.',[53]='/',[57]=' '
    };
    static const char shifted[] = {
        [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',
        [10]='(',[11]=')',[12]='_',[13]='+',[15]='\t',[16]='Q',[17]='W',
        [18]='E',[19]='R',[20]='T',[21]='Y',[22]='U',[23]='I',[24]='O',
        [25]='P',[26]='{',[27]='}',[30]='A',[31]='S',[32]='D',[33]='F',
        [34]='G',[35]='H',[36]='J',[37]='K',[38]='L',[39]=':',[40]='"',
        [43]='|',[44]='Z',[45]='X',[46]='C',[47]='V',[48]='B',[49]='N',
        [50]='M',[51]='<',[52]='>',[53]='?',[57]=' '
    };
    if (code >= sizeof(unshifted)) {
        return 0;
    }
    return shift ? shifted[code] : unshifted[code];
}
