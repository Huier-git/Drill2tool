'''''''''''''''''''''''''
'文件功能：EtherCAT总线扫描初始化
'数据参数请根据需要提前修改配置，其他请勿改动
'返回全局变量
'返回变量：ECAT_InitEnable    总线初始化状态   -1--未运行 0--初始化失败 1--初始化成功
'※注意※※※※ 使用前注意检查函数内全局初始化变量：SYS_ZFEATURE指令支持的轴数 ，手动创建对应坐标 ※※※※
'※注意※※※※ 使用前注意检查 Sub_SetNodePara 函数里是否有你使用的驱动型号 ※※※※
'※注意※※※※ Sub_SetNodePara 函数里将每圈脉冲数修改成 10000 ※※※※
'''''''''''''''''''''''''

'''''''''''''''''''''''''
'统一日志函数
'''''''''''''''''''''''''
GLOBAL SUB LogStage(stage$, detail$, value$)
	? "[", stage$, "]", " ", detail$, ": ", value$
ENDSUB

'''''''''''''''''''''''''
'安全的SDO写入函数（带错误检查）
'''''''''''''''''''''''''
GLOBAL FUNCTION SafeSdoWrite(slot, node, index, subindex, dataLen, value)
	LOCAL result
	result = SDO_WRITE(slot, node, index, subindex, dataLen, value)
	IF NOT result THEN
		LogStage("错误", "SDO写入失败", "节点=" + STR(node) + " 索引=0x" + HEX(index) + " 子索引=" + STR(subindex))
		SafeSdoWrite = 0
		RETURN
	ENDIF
	SafeSdoWrite = 1
ENDFUNCTION

'控制器支持轴数：
GLOBAL CONST ControlMaxAxis = SYS_ZFEATURE(0)
LogStage("系统初始化", "控制器支持轴数", STR(ControlMaxAxis))
'支持的真实轴数
GLOBAL CONST RealAxisMax = SYS_ZFEATURE(1)
LogStage("系统初始化", "支持的真实轴数", STR(RealAxisMax))

'单位号（低版本可不配置，缺省0，详见硬件手册）
GLOBAL CONST Bus_Slot = 0
'本地轴起始编号
GLOBAL CONST LocalAxis_Start = 0
'本地轴个数
GLOBAL CONST LocalAxis_Num = 0
'总线轴起始编号
GLOBAL CONST BusAxis_Start = 0

'总线初始化状态 -1--未运行 0--初始化失败 1--初始化成功
GLOBAL ECAT_InitEnable
	ECAT_InitEnable = -1
'延迟3秒，等待设备上电，不同的控制器上电的时间不同，根据控制器调整等待时间
DELAY(3000)

LogStage("系统初始化", "伺服通讯周期(us)", STR(SERVO_PERIOD))

GLOBAL NodeSum_Num, BusAxis_Num, NodeAxis_Num '设备个数，总线轴个数，每个节点上的电机数

ECAT_Init() '调用初始化函数

END
'/*************************************************************
'Description:		//总线及轴初始化
'Input:				//
'Input:				//
'Input:				//
'Output:			// ECAT_InitEnable=ON -->初始化完成标记
'Return:			//
'*************************************************************/
GLOBAL SUB ECAT_Init()

	LOCAL Drive_Vender,Drive_Device,Drive_Alias '设备的厂商编号，设备的设备编号，设备的设备别名ID

	RAPIDSTOP(2)
	'初始化清原有配置
	FOR i = 0 TO ControlMaxAxis - 1

		AXIS_ADDRESS(i) = 0
		AXIS_ENABLE(i) = 0
		ATYPE(i) = 0
		WAITIDLE(i)

	NEXT

	'配置本地轴映射
	FOR i=0 TO LocalAxis_Num -1
		AXIS_ADDRESS(LocalAxis_Start+i)= (0<<16) + i	'本地轴从0-->i映射到轴20-->20+i
		ATYPE(LocalAxis_Start+i)=0	'脉冲轴
	NEXT

	ECAT_InitEnable = -1

	SLOT_STOP(Bus_Slot)
	DELAY(200)

	SYSTEM_ZSET = SYSTEM_ZSET OR 128
	SLOT_SCAN(Bus_Slot)
	IF RETURN THEN

		NodeSum_Num = NODE_COUNT(Bus_Slot)
		LogStage("总线扫描", "扫描成功，设备总数", STR(NodeSum_Num))

		'总线轴计数器从0开始编号
		BusAxis_Num = 0

		FOR i = 0 TO NodeSum_Num - 1

			NodeAxis_Num = NODE_AXIS_COUNT(Bus_Slot,i) '获取设备轴数
			Drive_Vender = NODE_INFO(Bus_Slot,i,0) '获取设备厂商号
			Drive_Device = NODE_INFO(Bus_Slot,i,1) '获取设备号
			Drive_Alias = NODE_INFO(Bus_Slot,i,3) '获取设备别名ID

			'打印节点详细信息
			LogStage("节点扫描", "节点" + STR(i), "厂商=0x" + HEX(Drive_Vender) + " 设备=0x" + HEX(Drive_Device) + " 别名=" + STR(Drive_Alias) + " 轴数=" + STR(NodeAxis_Num))

			FOR j = 0 TO NodeAxis_Num - 1

				AXIS_ADDRESS(BusAxis_Num+BusAxis_Start) = BusAxis_Num + 1 '映射总线轴
				ATYPE(BusAxis_Num+BusAxis_Start) = 65  '设定控制模式 65-位置 66-速度 67-转矩 详细见表AXISSTATUS
				DRIVE_PROFILE(BusAxis_Num+BusAxis_Start) = -1 '总线轴PDO配置,保持默认参数-- -1 位置模式--0  速度模式--20+  转矩模式--30+
				DISABLE_GROUP(BusAxis_Num+BusAxis_Start) '每轴单独插补

				Sub_SetNodePara(i,Drive_Vender,Drive_Device)	'设定总线不同总线轴参数

				BusAxis_Num = BusAxis_Num + 1 '总线轴计数+1

			NEXT

		NEXT

		LogStage("总线扫描", "映射完成，总轴数", STR(BusAxis_Num))

		'打印轴映射表
		? "======== 轴映射表 ========"
		? "逻辑轴 | 物理地址 | 轴类型(ATYPE)"
		FOR i = BusAxis_Start TO BusAxis_Start + BusAxis_Num - 1
			? STR(i), " | ", STR(AXIS_ADDRESS(i)), " | ", STR(ATYPE(i))
		NEXT
		? "=========================="

		DELAY(100)
		SLOT_START(Bus_Slot)
		WA(3000) ' 延迟3秒，等待总线启动（不同控制器启动时间不同，根据控制器调整等待时间
		IF RETURN THEN

			LogStage("驱动初始化", "开始清除告警", "")
			FOR i = BusAxis_Start TO  BusAxis_Start + BusAxis_Num - 1

				BASE(i)
				DRIVE_CLEAR(0)
				WA(10)
				DRIVE_CONTROLWORD(i) = 128 ' 下发的控制字
				WA(10)
				DRIVE_CONTROLWORD(i)=6 ' 下发shutdown
				WA(10)
				DRIVE_CONTROLWORD(i)=7 ' 下发disable voltage
				WA(10)
				DRIVE_CONTROLWORD(i)=15 ' 下发fault reset
				WA(10)
			NEXT

			DELAY(100)
			LogStage("驱动初始化", "清除告警完成", "")
			DATUM(0) ' 初始化控制器的错误状态（


			DELAY(1000)
			LogStage("驱动初始化", "开始下发使能", "")
			WDOG = 1
			FOR i = BusAxis_Start TO  BusAxis_Start + BusAxis_Num - 1
				AXIS_ENABLE(i) = 1
			NEXT

			LogStage("驱动初始化", "伺服使能完成", "")

			'打印使能状态
			? "======== 伺服使能状态 ========"
			? "逻辑轴 | 使能状态(AXIS_ENABLE)"
			FOR i = BusAxis_Start TO BusAxis_Start + BusAxis_Num - 1
				? STR(i), " | ", STR(AXIS_ENABLE(i))
			NEXT
			? "=============================="

			ECAT_InitEnable = 1

			LogStage("驱动初始化", "开始配置轴参数", "")
			FOR i = BusAxis_Start TO  BusAxis_Start + BusAxis_Num - 1
				ACCEL(i) = 16384	' 4096*4
				DECEL(i) = 16384
				SPEED(i) = 16384
				FASTDEC(i) = 163840
			NEXT

			LogStage("总线初始化", "完成", "ECAT_InitEnable=" + STR(ECAT_InitEnable))

		ELSE
			LogStage("错误", "总线控制失败", "SLOT_START返回False")
			ECAT_InitEnable = 0
		ENDIF

	ELSE
		LogStage("错误", "总线扫描失败", "SLOT_SCAN返回False")
		ECAT_InitEnable = 0

	ENDIF

ENDSUB

'/*************************************************************
'Description:		//总线不同远端节点配置子函数
'Input:				//iNode -> 设备号
'Input:				//iVender -> 厂商编号
'Input:				//iDevice -> 设备号
'Output:			//
'Return:			//
'*************************************************************/
GLOBAL SUB Sub_SetNodePara(iNode,iVender,iDevice)

	LogStage("PDO配置", "节点" + STR(iNode), "厂商=0x" + HEX(iVender) + " 设备=0x" + HEX(iDevice))

	IF iVender = $9a AND iDevice = $30924 AND DRIVE_PROFILE(iNode) = -1 THEN ' ELMO(埃莫驱动)可在csp、csv、cst间切换模式

		'关闭PDO以允许修改
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1c12, $0, 5, $0) THEN GOTO PDO_ERROR
		WA(50)
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1c13, $0, 5, $0) THEN GOTO PDO_ERROR
		WA(50)

		'配置RxPDO映射
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $0, 5, $0) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $1, 7, $60410010) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $2, 7, $60770010) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $3, 7, $60640020) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $4, 7, $60fd0020) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $5, 7, $60b90010) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $6, 7, $60ba0020) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $7, 7, $60bb0020) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1a07, $0, 5, $7) THEN GOTO PDO_ERROR

		'配置TxPDO映射
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $0, 5, $0) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $1, 7, $60400010) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $2, 7, $60710010) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $3, 7, $60ff0020) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $4, 7, $607a0020) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $5, 7, $60b80010) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $6, 7, $60720010) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $7, 7, $60600008) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1607, $0, 5, $7) THEN GOTO PDO_ERROR

		'启用PDO
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1c12, $1, 6, $1607) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1c12, $0, 5, $1) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1c13, $1, 6, $1a07) THEN GOTO PDO_ERROR
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $1c13, $0, 5, $1) THEN GOTO PDO_ERROR

		'初始化状态字
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $6040, $0, 6, $0) THEN GOTO PDO_ERROR
		WA(50)
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $6040, $0, 6, $6) THEN GOTO PDO_ERROR
		WA(50)
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $6040, $0, 6, $7) THEN GOTO PDO_ERROR
		WA(50)
		IF NOT SafeSdoWrite(Bus_Slot, iNode, $6040, $0, 6, $F) THEN GOTO PDO_ERROR

		LogStage("PDO配置", "ELMO驱动节点" + STR(iNode), "配置完成")
		RETURN

PDO_ERROR:
		LogStage("错误", "ELMO驱动节点" + STR(iNode), "PDO配置失败，初始化中止")
		ECAT_InitEnable = 0
		RETURN

	ENDIF

ENDSUB


