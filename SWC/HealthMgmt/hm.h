/********************************************************************************
* COPYRIGHT (C) Vitesco Technologies 2025
* ALL RIGHTS RESERVED.
*
* The reproduction, transmission or use of this document or its
* contents is not permitted without express written authority.
* Offenders will be liable for damages. All rights, including rights
* created by patent grant or registration of a utility model or design,
* are reserved.
*********************************************************************************
*
*  File name:           $Source: hm.h $
*  Revision:            $Revision: 1.0 $
*  Author:              $Author: Li Song (uic59152)  $
*  Module acronym:      PWSM
*  Specification:
*  Date:                $Date: 2026/4/18  $
*
*  Description:     This Unit processes health management module
*
*********************************************************************************
*
*  Changes:
*
*
*********************************************************************************/

/***********************************************************************************************************************
*  include files
***********************************************************************************************************************/
#include "Platform.h"

typedef struct
{
    uint8 PartitionID; 
    uint8 NrOfPartitions; 
    uint8 PartitionState; 
    uint8 Reserved[4];
    uint8 NrOfRealTimeData;
}  HealthRealTimeData_st;

typedef struct
{
	uint8 PartitionId;
	uint8 Reserved[3];
	HealthRealTimeData_st RealTimeData;
} RealTimeInfo_st;


typedef struct
{
    uint8 ID[16];		/*故障唯一ID，各模块自行设计，ascii码形式*/
    uint32 Value[4];	/*最多4个32bits故障值，各个模块自行设计*/
    uint8 Category;		/*故障种类，系统自行设计*/
    uint8 PartitionID;	/*故障的发生Partition*/
    uint8 DetectedBy;	/*故障由哪一个模块/渠道发现，系统自行设计，比如DTC，总线*/
    uint8 Frequency;	/*故障发生次数*/
    uint8 Severity;		/*故障严重程度，系统自行设计*/
    uint8 Reserved[3];
}HealthHazard_st;

typedef struct
{
    uint8 Performance;/*性能，暂时未使用*/
    uint8 Reliability;/*可靠性，暂时未使用*/
    uint8 NrOfPartitions;/*一共多少个Partition，目前是A核1个，M核1个，一共2个*/
    uint8 PartitionState;/*Partition的状态*/
    uint8 HealthIndex;/*健康指数，暂时未使用*/
    uint8 Reserved[3];
}HealthIndicator_st;

typedef struct
{
    uint64 Timestamp; /*时间戳*/
    HealthIndicator_st Indicator; /*健康指标*/
    uint8 PartitionID;/*代表M核的Partition Id，目前是1*/
    uint16 CategoryMaxNrOfHazard[3]; /*CategoryMaxNrOfHazard[0]被CP的WDGM监控的SWC个数，[1],[2]未使用*/
    uint8 Reserved[4];
    uint8 NrOfHazards;/*故障信息个数*/
    HealthHazard_st Hazards[10]; /*故障信息*/
}HealthInfo_st;

typedef struct
{
    uint8   PartitionId;/*代表M核的Partition Id，目前是1*/            
    uint8   Reserved[3];                
    HealthInfo_st     HealthInfo;/*健康数据*/
} PICCHealthInfo_st;


/***********************************************************************************************************************
 *  Function name    : Hm_Init()
 *
 *  Description      : Initialize HM module and register with PICC driver
 *
 *  List of arguments: none
 *
 *  Return value     : noe
 *
 ***********************************************************************************************************************/
extern void Hm_Init(void);

/***********************************************************************************************************************
 *  Function name    : Hm_Main()
 *
 *  Description      : Implements all activities of the health State Manager.
 *
 *  List of arguments: none
 *
 *  Return value     : noe
 *
 ***********************************************************************************************************************/
extern void Hm_Main(void);
