#include "board.h"
#include "mw.h"
//本地编辑
core_t core;

extern rcReadRawDataPtr rcReadRawFunc;

// receiver read function
extern uint16_t pwmReadRawRC(uint8_t chan);

#ifdef USE_LAME_PRINTF
// gcc/GNU version
static void _putc(void *p, char c)
{
    serialWrite(core.mainport, c);
}
#else
// keil/armcc version
int fputc(int c, FILE *f)
{
    // let DMA catch up a bit when using set or dump, we're too fast.
    while (!isSerialTransmitBufferEmpty(core.mainport));
    serialWrite(core.mainport, c);
    return c;
}
#endif



int main(void)
{
    uint8_t i;
    drv_pwm_config_t pwm_params;
    drv_adc_config_t adc_params;
    serialPort_t* loopbackPort = NULL;

    systemInit();
#ifdef USE_LAME_PRINTF
    init_printf(NULL, _putc);
#endif

    checkFirstTime(false);
    readEEPROM();

    // configure power ADC
    if (mcfg.power_adc_channel > 0 && (mcfg.power_adc_channel == 1 || mcfg.power_adc_channel == 9))
        adc_params.powerAdcChannel = mcfg.power_adc_channel;
    else {
        adc_params.powerAdcChannel = 0;
        mcfg.power_adc_channel = 0;
    }

    adcInit(&adc_params);
    initBoardAlignment();

    // We have these sensors; SENSORS_SET defined in board.h depending on hardware platform
    sensorsSet(SENSORS_SET);

    mixerInit(); // this will set core.useServo var depending on mixer type
    // when using airplane/wing mixer, servo/motor outputs are remapped
    if (mcfg.mixerConfiguration == MULTITYPE_AIRPLANE || mcfg.mixerConfiguration == MULTITYPE_FLYING_WING)
        pwm_params.airplane = true;
    else
        pwm_params.airplane = false;
    pwm_params.useUART = feature(FEATURE_GPS) || feature(FEATURE_SERIALRX); // spektrum/sbus support uses UART too
    pwm_params.useSoftSerial = feature(FEATURE_SOFTSERIAL);
    pwm_params.usePPM = feature(FEATURE_PPM);
    pwm_params.enableInput = !feature(FEATURE_SERIALRX); // disable inputs if using spektrum
    pwm_params.useServos = core.useServo;
    pwm_params.extraServos = cfg.gimbal_flags & GIMBAL_FORWARDAUX;
    pwm_params.motorPwmRate = mcfg.motor_pwm_rate;
    pwm_params.servoPwmRate = mcfg.servo_pwm_rate;
    pwm_params.idlePulse = feature(FEATURE_3D) ? mcfg.neutral3d : 0;
    pwm_params.failsafeThreshold = cfg.failsafe_detect_threshold;
    switch (mcfg.power_adc_channel) {
        case 1:
            pwm_params.adcChannel = PWM2;
            break;
        case 9:
            pwm_params.adcChannel = PWM8;
            break;
        default:
            pwm_params.adcChannel = 0;
        break;
    }

    pwmInit(&pwm_params);

    // configure PWM/CPPM read function and max number of channels. spektrum or sbus below will override both of these, if enabled
    for (i = 0; i < RC_CHANS; i++)
        rcData[i] = 1502;
    rcReadRawFunc = pwmReadRawRC;
    core.numRCChannels = MAX_INPUTS;

    if (feature(FEATURE_SERIALRX)) {
        switch (mcfg.serialrx_type) {
            case SERIALRX_SPEKTRUM1024:
            case SERIALRX_SPEKTRUM2048:
                spektrumInit(&rcReadRawFunc);
                break;

            case SERIALRX_SBUS:
                sbusInit(&rcReadRawFunc);
                break;
        }
    } else { // spektrum and GPS are mutually exclusive
        // Optional GPS - available in both PPM and PWM input mode, in PWM input, reduces number of available channels by 2.
        // gpsInit will return if FEATURE_GPS is not enabled.
        // Sanity check below - protocols other than NMEA do not support baud rate autodetection
        if (mcfg.gps_type > 0 && mcfg.gps_baudrate < 0)
            mcfg.gps_baudrate = 0;
        gpsInit(mcfg.gps_baudrate);
    }
#ifdef SONAR
    // sonar stuff only works with PPM
    if (feature(FEATURE_PPM)) {
        if (feature(FEATURE_SONAR))
            Sonar_init();
    }
#endif

    LED1_ON;
    LED0_OFF;
    for (i = 0; i < 10; i++) {
        LED1_TOGGLE;
        LED0_TOGGLE;
        delay(25);
        BEEP_ON;
        delay(25);
        BEEP_OFF;
    }
    LED0_OFF;
    LED1_OFF;

    // drop out any sensors that don't seem to work, init all the others. halt if gyro is dead.
    sensorsAutodetect();
    imuInit(); // Mag is initialized inside imuInit

    // Check battery type/voltage
    if (feature(FEATURE_VBAT))
        batteryInit();

    serialInit(mcfg.serial_baudrate);

    if (feature(FEATURE_SOFTSERIAL)) {
      setupSoftSerial1(mcfg.softserial_baudrate, mcfg.softserial_inverted);
#ifdef SOFTSERIAL_LOOPBACK
      loopbackPort = (serialPort_t*)&(softSerialPorts[0]);
      serialPrint(loopbackPort, "LOOPBACK ENABLED\r\n");
#endif
    }

    if (feature(FEATURE_TELEMETRY))
        initTelemetry();

    previousTime = micros();
    if (mcfg.mixerConfiguration == MULTITYPE_GIMBAL)
        calibratingA = CALIBRATING_ACC_CYCLES;
    calibratingG = CALIBRATING_GYRO_CYCLES;
    calibratingB = CALIBRATING_BARO_CYCLES;             // 10 seconds init_delay + 200 * 25 ms = 15 seconds before ground pressure settles
    f.SMALL_ANGLES_25 = 1;

    // loopy
    while (1) {
        loop();
#ifdef SOFTSERIAL_LOOPBACK
        if (loopbackPort) {
            while (serialTotalBytesWaiting(loopbackPort)) {
    
                uint8_t b = serialRead(loopbackPort);
                serialWrite(loopbackPort, b);
                //serialWrite(core.mainport, b);
            };
        }
#endif
    }
}

void HardFault_Handler(void)
{
    // fall out of the sky
    writeAllMotors(mcfg.mincommand);
    while (1);
}
