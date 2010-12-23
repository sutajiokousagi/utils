
    {"SYS_APLL_LOCK", 0x7E00F000, 4},
    {"SYS_MPLL_LOCK", 0x7E00F004, 4},
    {"SYS_EPLL_LOCK", 0x7E00F008, 4},
    {"SYS_APLL_CON", 0x7E00F00C, 4},
    {"SYS_MPLL_CON", 0x7E00F010, 4},
    {"SYS_EPLL_CON0", 0x7E00F014, 4},
    {"SYS_EPLL_CON1", 0x7E00F018, 4},
    {"SYS_CLK_SRC", 0x7E00F01C, 4},
    {"SYS_CLK_DIV0", 0x7E00F020, 4},
    {"SYS_CLK_DIV1", 0x7E00F024, 4},
    {"SYS_CLK_DIV2", 0x7E00F028, 4},
    {"SYS_CLK_OUT", 0x7E00F02C, 4},
    {"SYS_HCLK_GATE", 0x7E00F030, 4},
    {"SYS_PCLK_GATE", 0x7E00F034, 4},
    {"SYS_SCLK_GATE", 0x7E00F038, 4},
    {"SYS_MEM0_CLK_GATE", 0x7E00F03C, 4},
    {"SYS_AHB_CON0", 0x7E00F100, 4},
    {"SYS_AHB_CON1", 0x7E00F104, 4},
    {"SYS_AHB_CON2", 0x7E00F108, 4},
    {"SYS_CLK_SRC2", 0x7E00F10C, 4},
    {"SYS_SDMA_SEL", 0x7E00F110, 4},
    {"SYS_SYS_ID", 0x7E00F118, 4},
    {"SYS_SYS_OTHERS", 0x7E00F11C, 4},
    {"SYS_MEM_SYS_CFG", 0x7E00F120, 4},
    {"SYS_QOS_OVERRIDE0", 0x7E00F124, 4},
    {"SYS_QOS_OVERRIDE1", 0x7E00F128, 4},
    {"SYS_MEM_CFG_STAT", 0x7E00F12C, 4},
    {"SYS_PWR_CFG", 0x7E00F804, 4},
    {"SYS_EINT_MASK", 0x7E00F808, 4},
    {"SYS_NORMAL_CFG", 0x7E00F810, 4},
    {"SYS_STOP_CFG", 0x7E00F814, 4},
    {"SYS_SLEEP_CFG", 0x7E00F818, 4},
    {"SYS_STOP_MEM_CFG", 0x7E00F81C, 4},
    {"SYS_OSC_FREQ", 0x7E00F820, 4},
    {"SYS_OSC_STABLE", 0x7E00F824, 4},
    {"SYS_PWR_STABLE", 0x7E00F828, 4},
    {"SYS_MTC_STABLE", 0x7E00F830, 4},
    {"SYS_BUS_CACHEABLE_CON", 0x7E00F838, 4},
    {"SYS_OTHERS", 0x7E00F900, 4},
    {"SYS_RST_STAT", 0x7E00F904, 4},
    {"SYS_WAKEUP_STAT", 0x7E00F908, 4},
    {"SYS_BLK_PWR_STAT", 0x7E00F90C, 4},
    {"SYS_INFORM0", 0x7E00FA00, 4},
    {"SYS_INFORM1", 0x7E00FA04, 4},
    {"SYS_INFORM2", 0x7E00FA08, 4},
    {"SYS_INFORM3", 0x7E00FA0C, 4},


    {"USB_HcRevision", 0x74300000, 4},
    {"USB_HcControl", 0x74300004, 4},
    {"USB_HcCommonStatus", 0x74300008, 4},
    {"USB_HcInterruptStatus", 0x7430000C, 4},
    {"USB_HcInterruptEnable", 0x74300010, 4},
    {"USB_HcInterruptDisable", 0x74300014, 4},
    {"USB_HcHCCA", 0x74300018, 4},
    {"USB_HcPeriodCuttentED", 0x7430001C, 4},
    {"USB_HcControlHeadED", 0x74300020, 4},
    {"USB_HcControlCurrentED", 0x74300024, 4},
    {"USB_HcBulkHeadED", 0x74300028, 4},
    {"USB_HcBulkCurrentED", 0x7430002C, 4},
    {"USB_HcDoneHead", 0x74300030, 4},
    {"USB_HcRmInterval", 0x74300034, 4},
    {"USB_HcFmRemaining", 0x74300038, 4},
    {"USB_HcFmNumber", 0x7430003C, 4},
    {"USB_HcPeriodicStart", 0x74300040, 4},
    {"USB_HcLSThreshold", 0x74300044, 4},
    {"USB_HcRhDescriptorA", 0x74300048, 4},
    {"USB_HcRhDescriptorB", 0x7430004C, 4},
    {"USB_HcRhStatus", 0x74300050, 4},
    {"USB_HcRhPortStatus1", 0x74300054, 4},
    {"USB_HcRhPortStatus2", 0x74300058, 4},


    {"IIC_CON", 0x7F004000, 4},
    {"IIC_STAT", 0x7F004004, 4},
    {"IIC_ADD",  0x7F004008, 4},
    {"IIC_DS",   0x7F00400C, 4},
    {"IIC_LC",   0x7F004010, 4},


    {"LCD_VIDOSD1D", 0x7710005C, 4},
    {"LCD_VIDOSD2A", 0x77100060, 4},
    {"LCD_VIDOSD2B", 0x77100064, 4},
    {"LCD_VIDOSD2C", 0x77100068, 4},
    {"LCD_VISOSD2D", 0x7710006C, 4},
    {"LCD_VIDOSD3A", 0x77100070, 4},
    {"LCD_VIDOSD3B", 0x77100074, 4},
    {"LCD_VIDOSD3C", 0x77100078, 4},
    {"LCD_VIDOSD4A", 0x77100080, 4},
    {"LCD_VIDOSD4B", 0x77100084, 4},
    {"LCD_VIDOSD4C", 0x77100088, 4},
    {"LCD_VIDW00ADD0B0", 0x771000A0, 4},
    {"LCD_VIDW00ADD0B1", 0x771000A4, 4},
    {"LCD_VIDW01ADD0B0", 0x771000A8, 4},
    {"LCD_VIDW01ADD0B1", 0x771000AC, 4},
    {"LCD_VIDW02ADD0", 0x771000B0, 4},
    {"LCD_VIDW03ADD0", 0x771000B8, 4},
    {"LCD_VIDW04ADD0", 0x771000C0, 4},
    {"LCD_VIDW00ADD1B0", 0x771000D0, 4},
    {"LCD_VIDW00ADD1B1", 0x771000D4, 4},
    {"LCD_VIDW01ADD1B0", 0x771000D8, 4},
    {"LCD_VIDW01ADD1B1", 0x771000DC, 4},
    {"LCD_VIDW02ADD1", 0x771000E0, 4},
    {"LCD_VIDW03ADD1", 0x771000E8, 4},
    {"LCD_VIDW04ADD1", 0x771000F0, 4},
    {"LCD_VIDW00ADD2", 0x77100100, 4},
    {"LCD_VIDW01ADD2", 0x77100104, 4},
    {"LCD_VIDW02ADD2", 0x77100108, 4},
    {"LCD_VIDW03ADD2", 0x7710010C, 4},
    {"LCD_VIDW04ADD2", 0x77100110, 4},
    {"LCD_VIDINTCON0", 0x77100130, 4},
    {"LCD_VIDINTCON1", 0x77100134, 4},
    {"LCD_W1KEYCON0", 0x77100140, 4},
    {"LCD_W1KEYCON1", 0x77100144, 4},
    {"LCD_W2KEYCON0", 0x77100148, 4},
    {"LCD_W2KEYCON1", 0x7710014C, 4},
    {"LCD_W3KEYCON0", 0x77100150, 4},
    {"LCD_W3KEYCON1", 0x77100154, 4},
    {"LCD_W4KEYCON0", 0x77100158, 4},
    {"LCD_DITHMODE", 0x77100170, 4},
    {"LCD_WIN0MAP", 0x77100180, 4},
    {"LCD_WIN1MAP", 0x77100184, 4},
    {"LCD_WIN2MAP", 0x77100188, 4},
    {"LCD_WIN3MAP", 0x7710018C, 4},
    {"LCD_WIN4MAP", 0x77100190, 4},
    {"LCD_WPALCON", 0x771001A0, 4},
    {"LCD_TRIGCON", 0x771001A4, 4},
    {"LCD_ITUIFCON0", 0x771001A8, 4},
    {"LCD_I80IFCONA0", 0x771001B0, 4},
    {"LCD_I80IFCONA1", 0x771001B4, 4},
    {"LCD_I80IFCONB0", 0x771001B8, 4},
    {"LCD_I80IFCONB1", 0x771001BC, 4},
    {"LCD_LDI_CMDCON0", 0x771001D0, 4},
    {"LCD_LDI_CMDCON1", 0x771001D4, 4},
    {"LCD_SIFCCON0", 0x771001E0, 4},
    {"LCD_SIFCCON1", 0x771001E4, 4},
    {"LCD_SIFCCON2", 0x771001E8, 4},
    {"LCD_LDI_CMD0", 0x77100280, 4},
    {"LCD_LDI_CMD1", 0x77100284, 4},
    {"LCD_LDI_CMD2", 0x77100288, 4},
    {"LCD_LDI_CMD3", 0x7710028C, 4},
    {"LCD_LDI_CMD4", 0x77100290, 4},
    {"LCD_LDI_CMD5", 0x77100294, 4},
    {"LCD_LDI_CMD6", 0x77100298, 4},
    {"LCD_LDI_CMD7", 0x7710029C, 4},
    {"LCD_LDI_CMD8", 0x771002A0, 4},
    {"LCD_LDI_CMD9", 0x771002A4, 4},
    {"LCD_LDI_CMD10", 0x771002A8, 4},
    {"LCD_LDI_CMD11", 0x771002AC, 4},
    {"LCD_W2PDATA01", 0x77100300, 4},
    {"LCD_W2PDATA23", 0x77100304, 4},
    {"LCD_W2PDATA45", 0x77100308, 4},
    {"LCD_W2PDATA67", 0x7710030C, 4},
    {"LCD_W2PDATA89", 0x77100310, 4},
    {"LCD_W2PDATAAB", 0x77100314, 4},
    {"LCD_W2PDATACD", 0x77100318, 4},
    {"LCD_W2PDATAEF", 0x7710031C, 4},
    {"LCD_W3PDATA01", 0x77100320, 4},
    {"LCD_W3PDATA23", 0x77100324, 4},
    {"LCD_W3PDATA45", 0x77100328, 4},
    {"LCD_W3PDATA67", 0x7710032C, 4},
    {"LCD_W3PDATA89", 0x77100330, 4},
    {"LCD_W3PDATAAB", 0x77100334, 4},
    {"LCD_W3PDATACD", 0x77100338, 4},
    {"LCD_W3PDATAEF", 0x7710033C, 4},
    {"LCD_W4PDATA01", 0x77100340, 4},
    {"LCD_W4PDATA23", 0x77100344, 4},


    {"GPIO_GPACON", 0x7F008000, 4},
    {"GPIO_GPADAT", 0x7F008004, 4},
    {"GPIO_GPAPUD", 0x7F008008, 4},
    {"GPIO_GPACONSLP", 0x7F00800C, 4},
    {"GPIO_GPAPUDSLP", 0x7F008010, 4},
    {"GPIO_GPBCON", 0x7F008020, 4},
    {"GPIO_GPBDAT", 0x7F008024, 4},
    {"GPIO_GPBPUD", 0x7F008028, 4},
    {"GPIO_GPBCONSLP", 0x7F00802C, 4},
    {"GPIO_GPBPUDSLP", 0x7F008030, 4},
    {"GPIO_GPCCON", 0x7F008040, 4},
    {"GPIO_GPCDAT", 0x7F008044, 4},
    {"GPIO_GPCPUD", 0x7F008048, 4},
    {"GPIO_GPCCONSLP", 0x7F00804C, 4},
    {"GPIO_GPCPUDSLP", 0x7F008050, 4},
    {"GPIO_GPDCON", 0x7F008060, 4},
    {"GPIO_GPDDAT", 0x7F008064, 4},
    {"GPIO_GPDPUD", 0x7F008068, 4},
    {"GPIO_GPDCONSLP", 0x7F00806C, 4},
    {"GPIO_GPDPUDSLP", 0x7F008070, 4},
    {"GPIO_GPECON", 0x7F008080, 4},
    {"GPIO_GPEDAT", 0x7F008084, 4},
    {"GPIO_GPEPUD", 0x7F008088, 4},
    {"GPIO_GPECONSLP", 0x7F00808C, 4},
    {"GPIO_GPEPUDSLP", 0x7F008090, 4},
    {"GPIO_GPFCON", 0x7F0080A0, 4},
    {"GPIO_GPFDAT", 0x7F0080A4, 4},
    {"GPIO_GPFPUD", 0x7F0080A8, 4},
    {"GPIO_GPFCONSLP", 0x7F0080AC, 4},
    {"GPIO_GPFPUDSLP", 0x7F0080B0, 4},
    {"GPIO_GPGCON", 0x7F0080C0, 4},
    {"GPIO_GPGDAT", 0x7F0080C4, 4},
    {"GPIO_GPGPUD", 0x7F0080C8, 4},
    {"GPIO_GPGCONSLP", 0x7F0080CC, 4},
    {"GPIO_GPGPUDSLP", 0x7F0080D0, 4},
    {"GPIO_GPHCON0", 0x7F0080E0, 4},
    {"GPIO_GPHCON1", 0x7F0080E4, 4},
    {"GPIO_GPHDAT", 0x7F0080E8, 4},
    {"GPIO_GPHPUD", 0x7F0080EC, 4},
    {"GPIO_GPHCONSLP", 0x7F0080F0, 4},
    {"GPIO_GPHPUDSLP", 0x7F0080F4, 4},
    {"GPIO_GPICON", 0x7F008100, 4},
    {"GPIO_GPIDAT", 0x7F008104, 4},
    {"GPIO_GPIPUD", 0x7F008108, 4},
    {"GPIO_GPICONSLP", 0x7F00810C, 4},
    {"GPIO_GPIPUDSLP", 0x7F008110, 4},
    {"GPIO_GPJCON", 0x7F008120, 4},
    {"GPIO_GPJDAT", 0x7F008124, 4},
    {"GPIO_GPJPUD", 0x7F008128, 4},
    {"GPIO_GPJCONSLP", 0x7F00812C, 4},
    {"GPIO_GPJPUDSLP", 0x7F008130, 4},
    {"GPIO_GPKCON0", 0x7F008800, 4},
    {"GPIO_GPKCON1", 0x7F008804, 4},
    {"GPIO_GPKDAT", 0x7F008808, 4},
    {"GPIO_GPKPUD", 0x7F00880C, 4},
    {"GPIO_GPLCON0", 0x7F008810, 4},
    {"GPIO_GPLCON1", 0x7F008814, 4},
    {"GPIO_GPLDAT", 0x7F008818, 4},
    {"GPIO_GPLPUD", 0x7F00881C, 4},
    {"GPIO_GPMCON", 0x7F008820, 4},
    {"GPIO_GPMDAT", 0x7F008824, 4},
    {"GPIO_GPMPUD", 0x7F008828, 4},
    {"GPIO_GPNCON", 0x7F008830, 4},
    {"GPIO_GPNDAT", 0x7F008834, 4},
    {"GPIO_GPNPUD", 0x7F008838, 4},
    {"GPIO_GPOCON", 0x7F008140, 4},
    {"GPIO_GPODAT", 0x7F008144, 4},
    {"GPIO_GPOPUD", 0x7F008148, 4},
    {"GPIO_GPOCONSLP", 0x7F00814C, 4},
    {"GPIO_GPOPUDSLP", 0x7F008150, 4},
    {"GPIO_GPPCON", 0x7F008160, 4},
    {"GPIO_GPPDAT", 0x7F008164, 4},
    {"GPIO_GPPPUD", 0x7F008168, 4},
    {"GPIO_GPPCONSLP", 0x7F00816C, 4},
    {"GPIO_GPPPUDSLP", 0x7F008170, 4},
    {"GPIO_GPQCON", 0x7F008180, 4},
    {"GPIO_GPQDAT", 0x7F008184, 4},
    {"GPIO_GPQPUD", 0x7F008188, 4},
    {"GPIO_GPQCONSLP", 0x7F00818C, 4},
    {"GPIO_GPQPUDSLP", 0x7F008190, 4},
    {"GPIO_SPCON", 0x7F0081A0, 4},
    {"GPIO_MEM0CONSLP0", 0x7F0081C0, 4},
    {"GPIO_MEM0CONSLP1", 0x7F0081C4, 4},
    {"GPIO_MEM1CONSLP", 0x7F0081C8, 4},
    {"GPIO_MEM0DRVCON", 0x7F0081D0, 4},
    {"GPIO_MEM1DRVCON", 0x7F0081D4, 4},
    {"GPIO_EINT0CON0", 0x7F008900, 4},
    {"GPIO_EINT0CON1", 0x7F008904, 4},
    {"GPIO_EINT0FLTCON0", 0x7F008910, 4},
    {"GPIO_EINT0FLTCON1", 0x7F008914, 4},
    {"GPIO_EINT0FLTCON2", 0x7F008918, 4},
    {"GPIO_EINTF0LTCON3", 0x7F00891C, 4},
    {"GPIO_EINT0MASK", 0x7F008920, 4},
    {"GPIO_EINT0PEND", 0x7F008924, 4},
    {"GPIO_SPCONSLP", 0x7F008880, 4},
    {"GPIO_SLPEN", 0x7F008930, 4},
    {"GPIO_EINT12CON", 0x7F008200, 4},
    {"GPIO_EINT34CON", 0x7F008204, 4},
    {"GPIO_EINT56CON", 0x7F008208, 4},
    {"GPIO_EINT78CON", 0x7F00820C, 4},
    {"GPIO_EINT9CON", 0x7F008210, 4},
    {"GPIO_EINT12FLTCON", 0x7F008220, 4},
    {"GPIO_EINT34FLTCON", 0x7F008224, 4},
    {"GPIO_EINT56FLTCON", 0x7F008228, 4},
    {"GPIO_EINT78FLTCON", 0x7F00822C, 4},
    {"GPIO_EINT9FLTCON", 0x7F008230, 4},
    {"GPIO_EINT12MASK", 0x7F008240, 4},
    {"GPIO_EINT34MASK", 0x7F008244, 4},
    {"GPIO_EINT56MASK", 0x7F008248, 4},
    {"GPIO_EINT78MASK", 0x7F00824C, 4},
    {"GPIO_EINT9MASK", 0x7F008250, 4},
    {"GPIO_EINT12PEND", 0x7F008260, 4},
    {"GPIO_EINT34PEND", 0x7F008264, 4},
    {"GPIO_EINT56PEND", 0x7F008268, 4},
    {"GPIO_EINT78PEND", 0x7F00826C, 4},
    {"GPIO_EINT9PEND", 0x7F008270, 4},
    {"GPIO_PRIORITY", 0x7F008280, 4},
    {"GPIO_SERVICE", 0x7F008284, 4},
    {"GPIO_SERVICEPEND", 0x7F008288, 4},

