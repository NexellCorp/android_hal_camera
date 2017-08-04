#!/system/bin/sh

TEST_COUNT=0
while :
do
	echo "TEST_COUNT --> $TEST_COUNT"
	input touchscreen tap 1000 280
	sleep 5
	TEST_COUNT=$((TEST_COUNT+1))
done
