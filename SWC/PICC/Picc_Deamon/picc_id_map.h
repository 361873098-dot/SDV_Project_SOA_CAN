/**
 * @file picc_id_map.h
 * @brief PICC Application ID Mapping Table
 *
 * Centralized definition of all PICC application LOCAL/REMOTE IDs.
 * LOCAL  = M-Core's own ProviderID/ConsumerID (used as localId)
 * REMOTE = A-Core peer's ConsumerID/ProviderID (used as remoteId)
 *
 * Naming rule: PICC_ID_<MODULE>_LOCAL / PICC_ID_<MODULE>_REMOTE
 *
 * Copyright 2024 NXP
 * All Rights Reserved.
 */

#ifndef PICC_ID_MAP_H
#define PICC_ID_MAP_H

/*==================================================================================================
 *                                         Power Management (M-Core=Server)
 *==================================================================================================*/

#define PICC_ID_PWR_LOCAL           (1U)    /* M-Core ProviderID */
#define PICC_ID_PWR_REMOTE          (6U)    /* A-Core ConsumerID */

/*==================================================================================================
 *                                         Device Management (M-Core=Server)
 *==================================================================================================*/

#define PICC_ID_DM_LOCAL            (2U)    /* M-Core ProviderID */
#define PICC_ID_DM_REMOTE           (7U)    /* A-Core ConsumerID */

/*==================================================================================================
 *                                         Health Management - Heartbeat (M-Core=Server)
 *==================================================================================================*/

#define PICC_ID_HEALTH_HB_LOCAL     (21U)   /* M-Core ProviderID */
#define PICC_ID_HEALTH_HB_REMOTE    (26U)   /* A-Core ConsumerID */

/*==================================================================================================
 *                                         Diagnostic Management (M-Core=Server)
 *==================================================================================================*/

#define PICC_ID_DIAGMGMT_LOCAL      (52U)   /* M-Core ProviderID (0x34) */
#define PICC_ID_DIAGMGMT_REMOTE     (60U)   /* A-Core ConsumerID (0x3C) */

/*==================================================================================================
 *                                         SOA Adapter (M-Core=Server)
 *==================================================================================================*/

#define PICC_ID_SOA_LOCAL           (71U)   /* M-Core ProviderID (0x47) */
#define PICC_ID_SOA_REMOTE          (76U)   /* A-Core ConsumerID (0x4C) */

/*==================================================================================================
 *                                         Health Management - Reporter (M-Core=Server)
 *==================================================================================================*/

#define PICC_ID_HEALTH_RPT_LOCAL    (81U)   /* M-Core ProviderID */
#define PICC_ID_HEALTH_RPT_REMOTE   (91U)   /* A-Core ConsumerID */

#endif /* PICC_ID_MAP_H */
