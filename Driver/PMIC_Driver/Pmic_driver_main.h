/**
 * @file Picc_main.h
 * @brief M-Core Inter-Core Communication Application - Header File
 *
 * IPC Shared Memory Driver application with PICC middleware.
 *
 * Copyright 2020-2024 NXP
 * All Rights Reserved.
 */

#ifndef PMIC_DRIVER_MAIN_H
#define PMIC_DRIVER_MAIN_H

#if defined(__cplusplus)
extern "C"{
#endif

/*  VR5510 Device Id register */
typedef union VR5510_DeviceIdRegTag
{
    uint16 Value;
    struct DeviceIdRegBits
    {
        uint16 DEV_ID           :4;     /* Device ID */
        uint16 FAM_ID           :4;     /* Family ID */
        uint16 MMREV           :3;     /* Metal Mask Revision */
        uint16 RESERVED1        :1;     /* Reserved */
        uint16 FMREV       	   :3;     /* Full Mask Revision */
        uint16 RESERVED2        :1;     /* Reserved */
    }B;
}VR5510_DeviceIdRegType;

/* VR5510 M_MODE register */
typedef union VR5510_MModeRegTag
{
    uint16 Value;
    struct MModeRegBits
    {
        uint16 STBY_PGOOD_TEST_EN            :1;     /* Enable or disable the Standby PGOOD test function (only available if OTP enable) */
        uint16 PWRON1DIS                     :1;     /* Disable the wake-up feature on PWRON1 input */
        uint16 PWRON2DIS                     :1;     /* Disable the wake-up feature on PWRON2 input */
        uint16 STBY_PGOOD_TEST_LVL       	 :1;     /* Change the STBY_PGOOD output level if STBY_PGOOD_TEST_EN = 1 */
        uint16 PWRON2_DSM_EN                 :1;     /* Enable / Disable Deep Sleep Mode request via the PWRON2 pin if DSM_EN_OTP = 1 */
        uint16 MAIN_NORMAL                   :1;     /* Main state machine status */
		uint16 EXT_FIN_DIS                   :1;    /* Disable the external FIN selection at PLL input   */
		uint16 PLL_LOCKED 		             :1;    /* Indicate if the PLL is locked */
        uint16 RESERVED                      :8;     /* Reserved */
    }B;
}VR5510_MModeRegType;

/* VR5510 M_FLAG1 register */
typedef union VR5510_MFlg1RegTag
{
    uint16 Value;
    struct MFlg1RegBits
    {
        uint16 LDO3_TSDFLG           :1;     /* LDO3 over temperature shutdown event */
        uint16 LDO2_TSDFLG           :1;     /* LDO2 over temperature shutdown event */
        uint16 LDO1_TSDFLG           :1;     /* LDO1 over temperature shutdown event */
        uint16 BUCK3_TSDFLG       	 :1;     /* BUCK3 over temperature shutdown event */
        uint16 BUCK2_TSDFLG          :1;     /* BUCK2 over temperature shutdown event */
        uint16 BUCK1_TSDFLG          :1;     /* BUCK1 over temperature shutdown event */
		uint16 BOOST_TSDFLG          :1;    /* BOOST over temperature shutdown event */
		uint16 HVLDO_TSDFLG 		 :1;    /* HVLDO over temperature shutdown event */
		uint16 LDO3OC                :1;    /* LDO3 over current event */
		uint16 LDO2OC				 :1;    /* LDO2 over current event */
		uint16 LDO1OC				 :1;    /* LDO1 over current event */
		uint16 BUCK3OC				 :1;    /* BUCK3 over current event */
		uint16 BUCK2OC				 :1;    /* BUCK2 over current event */
		uint16 BUCK1OC				 :1;    /* BUCK1 over current event */
        uint16 RESERVED              :1;     /* Reserved */
		uint16 HVLDOOC				 :1;    /* HVLDO over current event */
    }B;
}VR5510_MFlg1RegType;

/* VR5510 M_FLAG2 register */
typedef union VR5510_MFlg2RegTag
{
    uint16 Value;
    struct MFlg2RegBits
    {
        uint16 PWRON1FLG           :1;     /* PWRON1 wake up source flag */
        uint16 PWRON2FLG           :1;     /* PWRON2 wake up source flag */
        uint16 VSUPUVH             :1;     /* VSUP_UVH event */
        uint16 VSUPUVL       	   :1;     /* VSUP_UVL event */
        uint16 VSUPUV7             :1;     /* VSUP_UV7 event*/
        uint16 VPREUVH             :1;     /* VPRE_UVH event */
		uint16 VPREUVL             :1;    /* VPRE_UVL event */
		uint16 VPREOC 		       :1;    /* VPRE overcurrent event */
		uint16 VPRE_FB_OV          :1;    /* VPRE_FB_OV event */
		uint16 HVLDO_INPUT_UVL	   :1;    /* HVLDO input UVL event */
		uint16 STBY_TIMER_FLG	   :1;    /* STBY Timer event */
		uint16 VBOOSTOV			   :1;    /* VBOOST OV event */
		uint16 VBOOSTUVH		   :1;    /* VBOOST UVH event */
		uint16 VBOSUVH			   :1;    /* VBOS UVH event */
		uint16 TSD_BIST_ERR_FLG	   :1;    /* TSD BIST flag */
        uint16 DIE_CENTER_TEMPFLG  :1   /* Report a thermal event on the central thermal sensor */
    }B;
}VR5510_MFlg2RegType;

/* VR5510 M_FLAG3 register */
typedef union VR5510_MFlg3RegTag
{
    uint16 Value;
    struct MFlg3RegBits
    {
        uint16 I2C_M_REQ           :1;     /* Invalid main domain I2C access */
        uint16 I2C_M_CRC           :1;     /* I2C communication CRC error */
        uint16 PWRON1RT             :1;     /* Report event: PWRON1 real time state */
        uint16 PWRON2RT       	   :1;     /* Report event: PWRON2 real time state */
        uint16 RESERVED             :2;     /* Reserved */
        uint16 FIN_CLKWD_OK             :1;     /* CLK watchdog monitoring */
		uint16 LDO3_ST             :1;    /* LDO3 state */
		uint16 LDO2_ST 		       :1;    /* LDO2 state */
		uint16 LDO1_ST          :1;    /* LDO1 state */
		uint16 BUCK3_ST	   :1;    /* BUCK3 state */
		uint16 BUCK2_ST	   :1;    /* BUCK2 state */
		uint16 BUCK1_ST			   :1;    /* BUCK1 state */
		uint16 BOOST_ST		   :1;    /* BOOST state */
		uint16 HVLDO_ST			   :1;    /* HVLDO state */
		uint16 VPRE_ST	   :1;    /* VPRE state */

    }B;
}VR5510_MFlg3RegType;

/*==================================================================================================
 *                                         Function Declarations
 *==================================================================================================*/

/**
 * @brief Prepare and start main task
 */
void Pmic_driver_init(void);


#if defined(__cplusplus)
}
#endif

#endif /* MAIN_H */


