#include "Telemetry.h"
#include "Usart.h"

#define TELEMETRY_PERIOD_MS  125U

static volatile uint32 s_telemetryMs = 0U;

static const char* StateStr(ElevatorState_t s)
{
    switch (s)
    {
        case ELEV_IDLE:         return "IDLE";
        case ELEV_MOVING_UP:    return "MOVING_UP";
        case ELEV_MOVING_DOWN:  return "MOVING_DOWN";
        case ELEV_DOORS_OPEN:   return "DOORS_OPEN";
        case ELEV_EMERGENCY:    return "EMERGENCY";
        default:                return "UNKNOWN";
    }
}

static void TransmitUint8(uint8 val)
{
    char  buf[4];
    uint8 len = 0U;
    if (val == 0U) { Usart1_TransmitString("0"); return; }
    while (val > 0U && len < 3U)
    {
        buf[len++] = (char)('0' + (val % 10U));
        val       /= 10U;
    }
    uint8 i = 0U, j = (uint8)(len - 1U);
    while (i < j) { char t = buf[i]; buf[i] = buf[j]; buf[j] = t; i++; j--; }
    buf[len] = '\0';
    Usart1_TransmitString(buf);
}

void Telemetry_TickMs(void)
{
    s_telemetryMs++;
}

void Telemetry_Update(Elevator_t *elev)
{
    if (s_telemetryMs >= TELEMETRY_PERIOD_MS)
    {
        s_telemetryMs = 0U;
        Usart1_TransmitString("[TEL] ");
        Usart1_TransmitString(StateStr(elev->state));
        Usart1_TransmitString(" F:"); TransmitUint8(elev->current_floor);
        Usart1_TransmitString(" T:"); TransmitUint8(elev->target_floor);
        Usart1_TransmitString(" D:"); TransmitUint8(elev->direction);
        Usart1_TransmitString("\r\n");
    }
}