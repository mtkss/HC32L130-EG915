#include "network_strategy.h"
#include <stdio.h>
#include <string.h>
#include "serial.h"

#define DEBUG_MODE 1

OperatorProfile_t ru_ops[RU_OPERATOR_COUNT] = {
    {"25002", SIG_UNKNOWN}, /* MegaFon */
    {"25020", SIG_UNKNOWN}  /* Tele2 */
};

uint8_t active_op_idx = 0xFF;
uint8_t ru_attempted_mask = 0;
uint8_t ru_registered_mask = 0;
uint8_t ru_blocked_mask = 0;
uint8_t last_success_ru_op_idx = 0xFF;

static uint16_t last_confirmed_mcc = 0;

static SigStatus_t Test_Ping_Connectivity(void)
{
    Clear_RX_Buffer();
    AT_SendCmd("AT+CGATT?");
    if (!Wait_For_Easy_Response("+CGATT: 1", TIMEOUT_COUNT)) {
        return SIG_DEAD;
    }

    Clear_RX_Buffer();
    AT_SendCmd("AT+QIACT=1");
    Wait_For_Easy_Response("OK", TIMEOUT_COUNT);

    Clear_RX_Buffer();
    AT_SendCmd("AT+QPING=1,\"8.8.8.8\",4,1");
    if (Wait_For_Easy_Response("+QPING: 0", TIMEOUT_COUNT)) {
        return SIG_READY;
    }

    return SIG_BLOCKED;
}

static SigStatus_t Switch_And_Check(uint8_t idx)
{
    char cmd[64];
    SigStatus_t result;

    #if DEBUG_MODE
    SEGGER_RTT_printf(0,"[NET] Switching to PLMN %s\r\n", ru_ops[idx].plmn);
    #endif

    Clear_RX_Buffer();
    snprintf(cmd, sizeof(cmd), "AT+COPS=1,2,\"%s\",7",
             ru_ops[idx].plmn);
    AT_SendCmd(cmd);

    if (!Wait_For_Easy_Response("OK", SIM_TIMEOUT_COUNT)) {
        #if DEBUG_MODE
        SEGGER_RTT_printf(0,"[NET] PLMN %s COPS failed\r\n",
                          ru_ops[idx].plmn);
        #endif
        return SIG_DEAD;
    }

    result = Test_Ping_Connectivity();
    if (result == SIG_READY) {
        SEGGER_RTT_printf(0,"[NET] PLMN %s data ready\r\n",
                          ru_ops[idx].plmn);
    } else if (result == SIG_BLOCKED) {
        SEGGER_RTT_printf(0,"[NET] PLMN %s attached but data blocked\r\n",
                          ru_ops[idx].plmn);
    } else {
        SEGGER_RTT_printf(0,"[NET] PLMN %s not attached\r\n",
                          ru_ops[idx].plmn);
    }

    return result;
}

static uint8_t Switch_Back_To_Target(uint8_t target_idx)
{
    char cmd[64];

    if (active_op_idx != target_idx) {
        SEGGER_RTT_printf(0,"[NET] Switch back to usable PLMN %s\r\n",
                          ru_ops[target_idx].plmn);
        Clear_RX_Buffer();
        snprintf(cmd, sizeof(cmd), "AT+COPS=1,2,\"%s\",7",
                 ru_ops[target_idx].plmn);
        AT_SendCmd(cmd);
        if (!Wait_For_Easy_Response("OK", SIM_TIMEOUT_COUNT)) {
            SEGGER_RTT_printf(0,"[NET] Failed to return to PLMN %s\r\n",
                              ru_ops[target_idx].plmn);
            return STATUS_FAILED;
        }
    }

    Clear_RX_Buffer();
    AT_SendCmd("AT+QIACT=1");
    Wait_For_Easy_Response("OK", TIMEOUT_COUNT);
    active_op_idx = target_idx;
    return STATUS_OK;
}

void Network_Record_Confirmed_MCC(uint16_t mcc)
{
    if (mcc != 0u) {
        last_confirmed_mcc = mcc;
    }
}

uint16_t Network_Get_Last_Confirmed_MCC(void)
{
    return last_confirmed_mcc;
}

/*
 * Russia strategy:
 * 1. The first sweep order is MegaFon, then Tele2.
 * 2. An attached-but-blocked operator is considered seeded.
 * 3. A ready operator is remembered, but the first sweep continues so the
 *    remaining Russian operators can start their restriction period.
 * 4. Later wakeups test the last usable operator first. If both operators
 *    have attached before and it is still ready, the full sweep is skipped.
 */
uint8_t Run_Network_Strategy(void)
{
    static const uint8_t poll_order[RU_OPERATOR_COUNT] = {
        RU_MEGAFON_IDX, RU_TELE2_IDX
    };
    uint8_t tested_mask = 0;
    uint8_t target_idx = 0xFF;
    uint8_t all_registered;
    uint8_t order_pos;
    uint8_t idx;
    uint8_t bit;
    SigStatus_t sig;

    Network_Record_Confirmed_MCC(250u);
    all_registered = (ru_registered_mask ==
                      ((1u << RU_OPERATOR_COUNT) - 1u));

    SEGGER_RTT_printf(
        0,
        "[NET] Russia poll start attempted=0x%02X registered=0x%02X blocked=0x%02X last=%u\r\n",
        ru_attempted_mask,
        ru_registered_mask,
        ru_blocked_mask,
        last_success_ru_op_idx);

    if (last_success_ru_op_idx < RU_OPERATOR_COUNT) {
        idx = last_success_ru_op_idx;
        bit = (uint8_t)(1u << idx);
        sig = Switch_And_Check(idx);
        ru_ops[idx].last_sig = sig;
        ru_attempted_mask |= bit;
        tested_mask |= bit;
        active_op_idx = idx;

        if (sig == SIG_READY) {
            ru_registered_mask |= bit;
            ru_blocked_mask &= (uint8_t)(~bit);
            target_idx = idx;
            if (all_registered) {
                SEGGER_RTT_printf(
                    0,
                    "[NET] Last usable PLMN %s is ready; skip full poll\r\n",
                    ru_ops[idx].plmn);
                return idx;
            }
        } else if (sig == SIG_BLOCKED) {
            ru_registered_mask |= bit;
            ru_blocked_mask |= bit;
        }
    }

    for (order_pos = 0; order_pos < RU_OPERATOR_COUNT; order_pos++) {
        idx = poll_order[order_pos];
        bit = (uint8_t)(1u << idx);

        if ((tested_mask & bit) != 0u) {
            continue;
        }

        if ((target_idx != 0xFF) &&
            ((ru_registered_mask & bit) != 0u)) {
            continue;
        }

        sig = Switch_And_Check(idx);
        ru_ops[idx].last_sig = sig;
        ru_attempted_mask |= bit;
        tested_mask |= bit;
        active_op_idx = idx;

        if (sig == SIG_READY) {
            ru_registered_mask |= bit;
            ru_blocked_mask &= (uint8_t)(~bit);
            if (target_idx == 0xFF) {
                target_idx = idx;
            }
            last_success_ru_op_idx = idx;
        } else if (sig == SIG_BLOCKED) {
            ru_registered_mask |= bit;
            ru_blocked_mask |= bit;
            SEGGER_RTT_printf(
                0,
                "[NET] PLMN %s cooldown seeded\r\n",
                ru_ops[idx].plmn);
        } else {
            SEGGER_RTT_printf(
                0,
                "[NET] PLMN %s not attached; retry next wakeup\r\n",
                ru_ops[idx].plmn);
        }
    }

    if (target_idx == 0xFF) {
        SEGGER_RTT_printf(
            0,
            "[NET] Both Russian operators unusable; restore automatic selection\r\n");
        Restore_Auto_Network();
        active_op_idx = 0xFF;
        return 0xFF;
    }

    last_success_ru_op_idx = target_idx;
    if (Switch_Back_To_Target(target_idx) != STATUS_OK) {
        active_op_idx = 0xFF;
        return 0xFF;
    }

    SEGGER_RTT_printf(
        0,
        "[NET] Russia poll done target=%s registered=0x%02X blocked=0x%02X\r\n",
        ru_ops[target_idx].plmn,
        ru_registered_mask,
        ru_blocked_mask);
    return target_idx;
}
