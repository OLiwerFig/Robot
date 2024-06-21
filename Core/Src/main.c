/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>


#define UART_BUFFER_SIZE 			64
#define TIMER_CONF_BOTH_EDGE_T1T2	4
#define	TIMER_FREQUENCY				10
#define	SECOND_IN_MINUTE			60


#define ENCODER_1_RESOLUTION	14
#define ENCODER_2_RESOLUTION	14

#define MOTOR_1_GEAR			48
#define MOTOR_2_GEAR			48


#define WHEEL_RADIUS 			0.035
#define WHEEL_BASE 				0.15



/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void ProcessReceivedData(uint8_t* data, uint16_t length);
void SetTarget(float x, float y);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

volatile uint32_t speed_L = 0;
volatile uint32_t speed_R = 0;
volatile uint32_t count = 0;
volatile uint32_t count1 = 0;

uint8_t uartBuffer[UART_BUFFER_SIZE];

float speed_L_target, speed_R_target, pwm_R, pwm_L, dt;



volatile int pid_iterations = 0;
volatile float distance;




typedef struct {
    float Kp;
    float Ki;
    float Kd;
    float setpoint;
    float prev_error;
    float integral;
    float output;
    uint32_t prev_time;
} PID_TypeDef;


PID_TypeDef pid_L, pid_R;



void PID_Init(PID_TypeDef *pid, float Kp, float Ki, float Kd, float setpoint) {
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->setpoint = setpoint;
    pid->prev_error = 0;
    pid->integral = 0;
    pid->output = 0;
    pid->prev_time = HAL_GetTick();
}


void SetSpeed(PID_TypeDef *pid, float setpoint) {
	pid->setpoint = setpoint;

}



typedef struct {
    float x;
    float y;
    float theta;
} Odometry_TypeDef;

Odometry_TypeDef odom;

void Odometry_Init(Odometry_TypeDef *odom) {
    odom->x = 0.0f;
    odom->y = 0.0f;
    odom->theta = 0.0f;
}


void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim == &htim6) {

    	uint32_t encoder_count_L = __HAL_TIM_GET_COUNTER(&htim2);
        uint32_t encoder_count_R = __HAL_TIM_GET_COUNTER(&htim3);


        htim2.Instance->CNT = 0;
        htim3.Instance->CNT = 0;


        float distance_L = (float)encoder_count_L / 120.0f;
        float distance_R = (float)encoder_count_R / 120.0f;

        float pre_speed_L = distance_L * TIMER_FREQUENCY;
        if (pre_speed_L < 300) speed_L = pre_speed_L;

        float pre_speed_R = distance_R * TIMER_FREQUENCY;
        if (pre_speed_R < 300) speed_R = pre_speed_R;


    }
}


void Update_Odometry(Odometry_TypeDef *odom, float speed_L, float speed_R, float dt) {

    float v_L = speed_L / 100.0;
    float v_R = speed_R / 100.0;


    float v = (v_L + v_R) / 2.0;
    float omega = (v_R - v_L) / WHEEL_BASE;

    // Aktualizuj pozycje
    odom->theta += omega * dt;
    odom->x += v * cos(odom->theta) * dt;
    odom->y += v * sin(odom->theta) * dt;
}



typedef struct {
    float x;
    float y;
} Target_TypeDef;

Target_TypeDef target;


void SetTarget(float x, float y) {
    target.x = x;
    target.y = y;

    //char buffer[50];
    //int len = sprintf(buffer, "%.2f %.2f\n", x, y);
    //HAL_UART_Transmit(&huart2, (uint8_t *)buffer, len, HAL_MAX_DELAY);


}





void CalculateTargetSpeed(Odometry_TypeDef *odom, Target_TypeDef *target, float *speed_L_target, float *speed_R_target) {
    float dx = target->x - odom->x;
    float dy = target->y - odom->y;
    distance = sqrt(dx * dx + dy * dy);
    float angle_to_target = atan2(dy, dx);
    //printf("dx: %f, dy: %f, distance: %f\n\r", dx, dy, distance);



    float angle_error = angle_to_target - odom->theta;
    if (angle_error > M_PI) angle_error -= 2 * M_PI;
    if (angle_error < -M_PI) angle_error += 2 * M_PI;

    // Parametry do dostosowania
    float max_linear_speed = 120.0f;
    float max_angular_speed = 1000.0f;
    float linear_speed_kp = 60.0f;
    float angular_speed_kp = 10.0f;


    float linear_speed = linear_speed_kp * distance;
    if (linear_speed > max_linear_speed) {
        linear_speed = max_linear_speed;
    }

    float angular_speed = angular_speed_kp * angle_error;
    if (angular_speed > max_angular_speed) {
        angular_speed = max_angular_speed;
    } else if (angular_speed < -max_angular_speed) {
        angular_speed = -max_angular_speed;
    }


    *speed_L_target = linear_speed - (WHEEL_BASE / 2) * angular_speed;
    *speed_R_target = linear_speed + (WHEEL_BASE / 2) * angular_speed;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {

        ProcessReceivedData(uartBuffer, UART_BUFFER_SIZE);
        HAL_UART_Receive_IT(&huart2, uartBuffer, UART_BUFFER_SIZE);
    }
}



void UART_Transmit(const char *data) {
    HAL_UART_Transmit(&huart2, (uint8_t *)data, strlen(data), HAL_MAX_DELAY);
}

void ProcessReceivedData(uint8_t* data, uint16_t length)
{
    if (length >= sizeof(float) * 2) {
        float targetX, targetY;
        memcpy(&targetX, data, sizeof(float));
        memcpy(&targetY, data + sizeof(float), sizeof(float));
        SetTarget(targetX, targetY);
        //printf("Otrzymano targetX: %f, targetY: %f\n", 1.0f, 1.0f);

        //PID_Init(&pid_L, 3, 0.1, 0.2, targetY);
        //PID_Init(&pid_R, 3, 0.1, 0.2, targetX);
    }
}


void SetMotorDirection(int direction_L, int direction_R) {

    if (direction_L == 1) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET);
    }

    if (direction_R == 1) {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
    } else {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_SET);
    }
}


int _write(int file, char* ptr, int len){
	HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
	return len;
}

void SendDataToQt(Odometry_TypeDef *odom, Target_TypeDef *target ,float pwm_L ,float pwm_R,  float speed_L, float speed_R) {

    char buffer[100];
    sprintf(buffer, "%.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", speed_L, speed_R, pwm_L, pwm_R, odom->x, odom->y, odom->theta, target->x, target->y);
    HAL_UART_Transmit(&huart2, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);
}







float PID_Compute(PID_TypeDef *pid, float current_value, float dt) {
    float error = pid->setpoint - current_value;


    pid->integral += error * dt;


    float derivative = (error - pid->prev_error) / dt;


    pid->output = (pid->Kp * error) + (pid->Ki * pid->integral) + (pid->Kd * derivative);
    pid->prev_error = error;

    if (pid_iterations < 4) {
        if (pid->output > 30) pid->output  = 30;
        if (pid->output < 0) pid->output  = 0;
    } else {
        if (pid->output > 1000) pid->output  = 1000;
        if (pid->output < 0) pid->output  = 0;

    }

    pid_iterations++;

    return pid->output;
}








/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */


  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */

  HAL_UART_Receive_IT(&huart2, uartBuffer, UART_BUFFER_SIZE);


  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);


  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);

  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 0);

  HAL_TIM_Base_Start_IT(&htim6);

  Odometry_Init(&odom);

  uint32_t prev_time = HAL_GetTick();

  SetMotorDirection(0,0);

  PID_Init(&pid_L, 6, 1.5, 0.1, 1);
  PID_Init(&pid_R, 6, 1.5, 0.3, 1);


  SetTarget(0.5f,0);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

	    uint32_t current_time = HAL_GetTick();
	    dt = (current_time - prev_time) / 1000.0f;
	    prev_time = current_time;


	    // Aktulaizowanie polozenia
	    Update_Odometry(&odom, speed_L, speed_R, dt);


	    // Obliczanie predkosci
	    CalculateTargetSpeed(&odom, &target, &speed_L_target, &speed_R_target);


	    // Sprawdzenie, czy robot osiągnął cel
	    if (distance < 0.1) {
	        speed_L_target = 0;
	        speed_R_target = 0;
	        pid_L.integral = 0;
	        pid_R.integral = 0;
	        //printf("dojechales do celuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuuu");
	    }


	    // Ustawanie predkosci do regulatorow pid
	    pid_L.setpoint = speed_L_target;
	    pid_R.setpoint = speed_R_target;

	    //pid_L.setpoint = 50;
	    //pid_R.setpoint = 50;


	    // Obliczanie wypeniania PWM przez regultor pid
	    pwm_L = PID_Compute(&pid_L, speed_L, dt);
	    pwm_R = PID_Compute(&pid_R, speed_R, dt);

	    //__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 100);
	    //__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 100);


	    // Nadawanie silnika odpowiedniego pwm
	    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, pwm_L);
	    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, pwm_R);


	    SendDataToQt(&odom, &target ,pwm_L ,pwm_R ,speed_L ,speed_R);




	    //printf("PWM values - Left: %.2f, Right: %.2f\n\r", pwm_L, pwm_R);

	    /*
	    printf("Position: x = %.2f, y = %.2f, theta = %.2f dystans do celu jest rowny : %.2f\n\r", odom.x, odom.y, odom.theta, distance_to_target);
	    printf("Target: x = %.2f, y = %.2f \n\r", target.x, target.y);
	    printf("Speed L target: %.2f, Speed R target: %.2f\n\r", speed_L_target, speed_R_target);
	    printf("rzzezczywiste predkosci  rowne lewe kolo  : %.2ld, prawe kolo : %.2ld\n\r", speed_L, speed_R);
		*/


	    HAL_Delay(100);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
