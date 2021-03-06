#include "Navigate.h"
#include "Math.h"
#include "Imu.h"
#include "kalman3.h"
#include "kalmanVel.h"
#include "include.h"
#include "opticalflow.h"
#include "malloc.h"	
#include "MotionCal.h"
NAVGATION_t nav;
KalmanVel_t kalmanVel;
Kalman_t kalmanPos;

static void KalmanVelInit(void);
static void KalmanPosInit(void);

/**********************************************************************************************************
*函 数 名: NavigationInit
*功能说明: 导航参数初始化
*形    参: 无
*返 回 值: 无
**************************************************************************************************/
void NavigationInit(void)
{
    KalmanVelInit();
    KalmanPosInit();
}

/**********************************************************************************************************
*函 数 名: VelocityEstimate
*功能说明: 飞行速度估计 融合加速度、GPS、气压计及TOF等多个传感器的数据
*          速度的计算均在机体坐标系下进行，所以GPS速度在参与融合时需要先转换到机体坐标系
*形    参: 无
*返 回 值: 无
**************************************************************************************************/
void VelocityEstimate(void)
{
    static uint64_t previousT;
    float deltaT;
    static uint32_t count;
    static bool fuseFlag;
        
    ////计算时间间隔，用于积分
    deltaT = (GetSysTime_us() - previousT) * 1e-6f;
    deltaT = ConstrainFloat(deltaT, 0.0005, 0.005);
    previousT = GetSysTime_us();

    //获取运动加速度
    nav.accel.x = -imu_data.h_acc[Y]*1.5f;
    nav.accel.y = -imu_data.h_acc[X]*1.5f;
    nav.accel.z = imu_data.w_acc[Z]*1e-3f;
                                                                                           
    //加速度数据更新频率1KHz，而气压数据更新频率只有25Hz，GPS数据只有10Hz
    //这里将气压与GPS参与融合的频率强制统一为25Hz
    if(count++ % 40 == 0)
    {
        nav.velMeasure[0] = user_flow_x*10;           //光流X轴速度
        nav.velMeasure[1] = user_flow_y*10;           //光流Y轴速度
        nav.velMeasure[2] = wcz_ref_speed;                  //Z轴气压计速度
        nav.velMeasure[3] = 0;              //Z轴气压计速度
        nav.velMeasure[4] = 0;                              //TOF激光速度
        nav.velMeasure[5] = 0; 
        
        KalmanVelUseMeasurement(&kalmanVel, TOF_VEL, false);
        
        fuseFlag = true;
    }
    else
    {
        fuseFlag = false;
    }
    

    KalmanVelUpdate(&kalmanVel, &nav.velocity, &nav.accel_bias, nav.accel, nav.velMeasure, deltaT, fuseFlag);

}


void PositionEstimate(void)
{
    static uint64_t previousT;
    float deltaT;
    Vector3f_t input;
    static uint32_t count;
    static bool fuseFlag;


    deltaT = (GetSysTime_us() - previousT) * 1e-6;
    deltaT = ConstrainFloat(deltaT, 0.0005, 0.002);
    previousT = GetSysTime_us();


    if(count++ % 40 == 0)
    {	
		nav.posMeasure.x = mini_flow.x_i;
		nav.posMeasure.y = mini_flow.y_i;
		nav.posMeasure.z = wcz_ref_height;
        fuseFlag = true;
    }
    else
    {
        fuseFlag = false;
    }
    //速度积分
    input.x = nav.velocity.x * deltaT;
    input.y = nav.velocity.y * deltaT;
    input.z = nav.velocity.z * deltaT;

    //位置更新
    KalmanUpdate(&kalmanPos, input, nav.posMeasure, fuseFlag);
    nav.position = kalmanPos.state;
}


/**********************************************************************************************************
*函 数 名: KalmanVelInit
*功能说明: 飞行速度估计的卡尔曼结构体初始化
*形    参: R固定，Q越大，代表越信任侧量值，Q无穷代表只用测量值；反之，Q越小代表越信任模型预测值，Q为零则是只用模型预测。
*返 回 值: 无 q 0.2 r 5 还阔以
**************************************************************************************************************************************************/
static void KalmanVelInit(void)
{
    float qMatInit[6][6] = {{0.1, 0, 0, 0, 0, 0},
                            {0, 0.1, 0, 0, 0, 0},
                            {0, 0, 0.05, 0, 0, 0},      
                            {0.03, 0, 0, 0, 0, 0},
                            {0, 0.03, 0, 0, 0, 0},
                            {0, 0, 0.03, 0, 0, 0}};

    float rMatInit[6][6] = {{200, 0, 0, 0, 0, 0},          //速度x轴数据噪声方差
                            {0, 200, 0, 0, 0, 0},          //速度y轴数据噪声方差
                            {0, 0, 200, 0, 0, 0},          //GPS速度z轴数据噪声方差       
                            {0, 0, 0, 2500, 0, 0},         //气压速度数据噪声方差
                            {0, 0, 0, 0, 2000, 0},         //TOF速度数据噪声方差
                            {0, 0, 0, 0, 0, 500000}};      //z轴速度高通滤波系数

    float pMatInit[6][6] = {{20, 0, 0, 0, 0, 0},
                            {0, 20, 0, 0, 0, 0},
                            {0, 0, 5, 0, 0, 0},      
                            {2, 0, 0, 2, 0, 0},
                            {0, 2, 0, 0, 2, 0},
                            {0, 0, 6, 0, 0, 2}};    //增大协方差P的初值，可以提高初始化时bias的收敛速度

    float hMatInit[6][6] = {{1, 0, 0, 0, 0, 0},
                            {0, 1, 0, 0, 0, 0},
                            {0, 0, 1, 0, 0, 0},      
                            {0, 0, 1, 0, 0, 0},
                            {0, 0, 1, 0, 0, 0},
                            {0, 0, 1, 0, 0, 0}};    //h[5][2]:速度z轴增加少许高通滤波效果�

    float fMatInit[6][6] = {{1, 0, 0, 0, 0, 0},
                            {0, 1, 0, 0, 0, 0},
                            {0, 0, 1, 0, 0, 0},      
                            {0, 0, 0, 1, 0, 0},
                            {0, 0, 0, 0, 1, 0},
                            {0, 0, 0, 0, 0, 1}};
    
    float bMatInit[6][6] = {{1, 0, 0, 0, 0, 0},
                            {0, 1, 0, 0, 0, 0},
                            {0, 0, 1, 0, 0, 0},      
                            {0, 0, 0, 0, 0, 0},
                            {0, 0, 0, 0, 0, 0},
                            {0, 0, 0, 0, 0, 0}};
    
	//初始化卡尔曼滤波器的相关矩阵
    KalmanVelQMatSet(&kalmanVel, qMatInit);
    KalmanVelRMatSet(&kalmanVel, rMatInit);
    KalmanVelCovarianceMatSet(&kalmanVel, pMatInit);
    KalmanVelObserveMapMatSet(&kalmanVel, hMatInit);
    KalmanVelStateTransMatSet(&kalmanVel, fMatInit);
    KalmanVelBMatSet(&kalmanVel, bMatInit);
                            
    ////状态滑动窗口，用于解决卡尔曼状态估计量与观测量之间的相位差问题
	kalmanVel.slidWindowSize = 250;
    kalmanVel.stateSlidWindow = mymalloc(kalmanVel.slidWindowSize * sizeof(Vector3f_t));
    kalmanVel.fuseDelay[FLOW_VEL_X] = 50;    //速度x轴数据延迟参数：0.22s
    kalmanVel.fuseDelay[FLOW_VEL_Y] = 50;    //速度y轴数据延迟参数：0.22s
    kalmanVel.fuseDelay[FLOW_VEL_Z] = 50;    //速度z轴数据延迟参数：0.22s
    kalmanVel.fuseDelay[BARO_VEL]  = 50;     //气压速度数据延迟参数：0.1s
    kalmanVel.fuseDelay[TOF_VEL]   = 30;     //TOF速度数据延迟参数：
}

/**********************************************************************************************************
*函 数 名: KalmanPosInit
*功能说明: 位置估计的卡尔曼结构体初始化
*形    参: 无
*返 回 值: 无
*****************************************************************************************************************/
static void KalmanPosInit(void)
{
    float qMatInit[9] = {0.5, 0, 0, 0, 0.5, 0, 0, 0, 0.5};
    float rMatInit[9] = {20, 0,  0, 0,20, 0, 0, 0, 50};
    float pMatInit[9] = {10, 0, 0, 0, 10, 0, 0, 0, 10};
    float fMatInit[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float hMatInit[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float bMatInit[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    //初始化卡尔曼滤波器的相关矩阵  
	KalmanQMatSet(&kalmanPos, qMatInit);
    KalmanRMatSet(&kalmanPos, rMatInit);
    KalmanBMatSet(&kalmanPos, bMatInit);
    KalmanCovarianceMatSet(&kalmanPos, pMatInit);
    KalmanStateTransMatSet(&kalmanPos, fMatInit);
    KalmanObserveMapMatSet(&kalmanPos, hMatInit);

    //状态滑动窗口，用于解决卡尔曼状态估计量与观测量之间的相位差问题
    kalmanPos.slidWindowSize = 200;
    kalmanPos.statusSlidWindow = mymalloc(kalmanPos.slidWindowSize * sizeof(kalmanPos.state));
    kalmanPos.fuseDelay.x = 20;    //0.05s延时
    kalmanPos.fuseDelay.y = 20;    //0.05s延时
    kalmanPos.fuseDelay.z = 100;    //0.05s延时
}

/**********************************************************************************************************
*函 数 名: NavigationReset
*功能说明: 导航相关数据复位
*形    参: 无
*返 回 值: 无
***************************************************************************************************/
void NavigationReset(void)
{
    kalmanVel.state[0] = 0;
    kalmanVel.state[1] = 0;
    kalmanVel.state[2] = 0;

//    if(GpsGetFixStatus())
//    {
    kalmanPos.state.x = mini_flow.x_i;
    kalmanPos.state.y = mini_flow.y_i;
//    }
//    else
//    {
//        kalmanPos.state.x = 0;
//        kalmanPos.state.y = 0;
//    }
    kalmanPos.state.z = baro_height;
}

/**********************************************************************************************************
*函 数 名: GetCopterAccel
*功能说明: 获取飞行加速度
*形    参: 无
*返 回 值: 加速度值
************************************************************************************/
Vector3f_t GetCopterAccel(void)
{
    return nav.accel;
}

/**********************************************************************************************************
*函 数 名: GetAccelBias
*功能说明: 获取加速度bias
*形    参: 无
*返 回 值: 加速度bias值
**********************************************************************************************************/
Vector3f_t GetAccelBias(void)
{
    return nav.accel_bias;
}

/**********************************************************************************************************
*函 数 名: GetCopterVelocity
*功能说明: 获取飞行速度估计值
*形    参: 无
*返 回 值: 速度值
**********************************************************************************************************/
Vector3f_t GetCopterVelocity(void)
{
    return nav.velocity;
}

/**********************************************************************************************************
*函 数 名: GetCopterVelMeasure
*功能说明: 获取飞行速度测量值
*形    参: 无
*返 回 值: 速度值
**********************************************************************************************************/
float* GetCopterVelMeasure(void)
{
    return nav.velMeasure;
}

/**********************************************************************************************************
*函 数 名: GetCopterPosition
*功能说明: 获取位置估计值
*形    参: 无
*返 回 值: 位置值
**********************************************************************************************************/
Vector3f_t GetCopterPosition(void)
{
    return nav.position;
}

/**********************************************************************************************************
*函 数 名: GetCopterPosMeasure
*功能说明: 获取位置测量值
*形    参: 无
*返 回 值: 速度值
**********************************************************************************************************/
Vector3f_t GetCopterPosMeasure(void)
{
    return nav.posMeasure;
}
