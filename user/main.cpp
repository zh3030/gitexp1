#include "stm32f10x.h"
#include <stdint.h>

/*========================== 硬件引脚定义 ==========================*/
// RGB 灯：PA2（红）和 PA3（绿）
#define RGB_R_PIN       GPIO_Pin_2
#define RGB_G_PIN       GPIO_Pin_3
#define RGB_PORT        GPIOA
#define RGB_RCC         RCC_APB2Periph_GPIOA

// 按键：PB0（按键1，短按），PB1（按键2，长按）
#define KEY1_PIN        GPIO_Pin_0
#define KEY2_PIN        GPIO_Pin_1
#define KEY_PORT        GPIOB
#define KEY_RCC         RCC_APB2Periph_GPIOB

/*========================== 全局变量 ==========================*/
/**
 * @brief RGB 工作模式（0=绿灯常亮，1=红灯常亮，2=绿灯呼吸）
 */
uint8_t rgb_mode = 0;

/**
 * @brief 显示使能标志
 *       1 = 允许根据模式显示RGB（正常状态）
 *       0 = 禁止显示（灯全灭，用于长按后松手）
 */
uint8_t display_enabled = 1;

/**
 * @brief 按键2长按状态（用于检测按下和松手）
 */
uint8_t key2_holding = 0;

/*========================== 延时函数 ==========================*/
/**
 * @brief 微秒级延时（72MHz 主频下约精确）
 * @param us 微秒数
 */
void delay_us(uint32_t us)
{
    uint32_t i;
    for (; us > 0; us--)
        for (i = 8; i > 0; i--);
}

/**
 * @brief 毫秒级延时
 * @param ms 毫秒数
 */
void delay_ms(uint16_t ms)
{
    for (; ms > 0; ms--)
        delay_us(1000);
}

/*========================== RGB 控制函数 ==========================*/
/**
 * @brief 初始化 RGB 引脚为推挽输出
 */
void RGB_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    RCC_APB2PeriphClockCmd(RGB_RCC, ENABLE);

    GPIO_InitStruct.GPIO_Pin   = RGB_R_PIN | RGB_G_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RGB_PORT, &GPIO_InitStruct);

    // 默认全部熄灭
    GPIO_ResetBits(RGB_PORT, RGB_R_PIN);
    GPIO_ResetBits(RGB_PORT, RGB_G_PIN);
}

/**
 * @brief 绿灯常亮（红灯熄灭）
 */
void RGB_SetGreenOn(void)
{
    GPIO_SetBits(RGB_PORT, RGB_G_PIN);
    GPIO_ResetBits(RGB_PORT, RGB_R_PIN);
}

/**
 * @brief 红灯常亮（绿灯熄灭）
 */
void RGB_SetRedOn(void)
{
    GPIO_SetBits(RGB_PORT, RGB_R_PIN);
    GPIO_ResetBits(RGB_PORT, RGB_G_PIN);
}

/**
 * @brief 全部熄灭
 */
void RGB_AllOff(void)
{
    GPIO_ResetBits(RGB_PORT, RGB_R_PIN);
    GPIO_ResetBits(RGB_PORT, RGB_G_PIN);
}

/**
 * @brief 绿灯呼吸灯效果（仅绿灯动作，红灯始终保持熄灭）
 * @note  非阻塞式，需在主循环中反复调用
 *        周期约 1 秒，渐亮渐暗
 */
void RGB_Green_Breathe(void)
{
    static uint16_t step = 0;        // PWM 占空比步进
    static uint8_t  direction = 0;   // 0=渐亮, 1=渐暗

    // 红灯始终熄灭（符合“呼吸灯红灯不点亮”要求）
    GPIO_ResetBits(RGB_PORT, RGB_R_PIN);

    if (direction == 0) {  // 渐亮
        GPIO_SetBits(RGB_PORT, RGB_G_PIN);
        delay_us(step);
        GPIO_ResetBits(RGB_PORT, RGB_G_PIN);
        delay_us(1000 - step);
        step++;
        if (step >= 1000) {
            step = 999;
            direction = 1;
        }
    } else {               // 渐暗
        GPIO_SetBits(RGB_PORT, RGB_G_PIN);
        delay_us(step);
        GPIO_ResetBits(RGB_PORT, RGB_G_PIN);
        delay_us(1000 - step);
        step--;
        if (step == 0) {
            direction = 0;
        }
    }
}

/*========================== 按键扫描函数 ==========================*/
/**
 * @brief 初始化按键引脚（上拉输入）
 */
void KEY_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    RCC_APB2PeriphClockCmd(KEY_RCC, ENABLE);

    GPIO_InitStruct.GPIO_Pin   = KEY1_PIN | KEY2_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IPU;  // 内部上拉，按下为低电平
    GPIO_Init(KEY_PORT, &GPIO_InitStruct);
}

/**
 * @brief 按键1短按检测（带消抖）
 * @retval 1 表示有效短按，0 表示无动作
 */
uint8_t KEY_Scan_ShortPress(void)
{
    if (GPIO_ReadInputDataBit(KEY_PORT, KEY1_PIN) == 0) {
        delay_ms(20);
        if (GPIO_ReadInputDataBit(KEY_PORT, KEY1_PIN) == 0) {
            // 等待释放
            while (GPIO_ReadInputDataBit(KEY_PORT, KEY1_PIN) == 0);
            delay_ms(20);
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 按键2长按实时检测（不等待释放）
 * @retval 1 表示当前被按住，0 表示未被按住
 */
uint8_t KEY_Scan_LongPress(void)
{
    if (GPIO_ReadInputDataBit(KEY_PORT, KEY2_PIN) == 0) {
        delay_ms(20);
        if (GPIO_ReadInputDataBit(KEY_PORT, KEY2_PIN) == 0) {
            return 1;
        }
    }
    return 0;
}

/*========================== 主程序 ==========================*/
int main(void)
{
    // 系统时钟默认 72MHz（启动文件已配置）
    RGB_GPIO_Init();
    KEY_GPIO_Init();

    // 初始状态：全灭
    RGB_AllOff();

    while (1)
    {
        // ---------- 按键1短按处理 ----------
        if (KEY_Scan_ShortPress() == 1) {
            // 切换模式（0->1->2->0...）
            rgb_mode = (rgb_mode + 1) % 3;
            // 一旦有按键1按下，恢复显示使能（取消长按的禁止显示）
            display_enabled = 1;
        }

        // ---------- 按键2长按处理 ----------
        if (KEY_Scan_LongPress() == 1) {
            // 长按被按住
            key2_holding = 1;
            // 按下时绿灯常亮，并禁止模式显示（覆盖其他模式）
            display_enabled = 0;     // 禁止自动显示
            RGB_SetGreenOn();        // 绿灯亮
        } else {
            // 按键2松手
            if (key2_holding == 1) {
                key2_holding = 0;
                // 松手后立即熄灭所有灯
                RGB_AllOff();
                // 注意：display_enabled 保持为 0，这样灯不会自动重新亮起
                // 直到用户再次按按键1，才会使能显示并切换模式
            }
        }

        // ---------- 正常显示模式（仅当 display_enabled == 1） ----------
        if (display_enabled == 1) {
            switch (rgb_mode)
            {
                case 0:
                    RGB_SetGreenOn();        // 绿灯常亮
                    break;
                case 1:
                    RGB_SetRedOn();          // 红灯常亮
                    break;
                case 2:
                    RGB_Green_Breathe();     // 绿灯呼吸（红灯不亮）
                    break;
                default:
                    RGB_AllOff();
                    break;
            }
        }
        // 如果 display_enabled == 0，则保持全灭（已由按键2松手时关闭）

        // 给呼吸灯提供足够的时间步进，同时保证按键扫描频率合适
        delay_ms(5);
    }
}
