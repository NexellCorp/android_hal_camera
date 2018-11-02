#!/system/bin/sh

TEST_COUNT=0
while :
do
	echo "TEST_COUNT --> $TEST_COUNT"
	#navi_ref, avn_ref
	#input touchscreen tap 1000 280
	#clova
	#cat /proc/meminfo | grep Cma*
	cat /proc/208/status | grep VmData
	# preview start <-> stop test generalized coordinates 1200*400, 1300*400
	# capture and recording test generalized coordinates 1200*400
	#input touchscreen tap 1200 400
	#input touchscreen tap 1150 700
	#input touchscreen tap 600 780
	#input touchscreen tap 332 567
	#am start -n com.android.camera2/com.android.camera.CameraActivity
	# artik board android camera app recording/capture button
	#input touchscreen tap 954 281
	# sds recording aging test
	input touchscreen tap 503 501
	sleep 5
	#cat /proc/meminfo | grep Cma*
	#input keyevent 4/*back key*/
	#input touchscreen tap 1200 400
	#input touchscreen tap 600 780
	#input touchscreen tap 471 791
	# artik board android camera app back key
	#input touchscreen tap 332 567
	# artik board android camera app recording/capture button
	#input touchscreen tap 954 281
	# sds recording aging test
	cat /proc/208/status | grep VmData
	input touchscreen tap 503 501 
	sleep 5
	TEST_COUNT=$((TEST_COUNT+1))
done
