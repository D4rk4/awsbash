#!/bin/bash
#
# Author Dmitry A. Galenko <dmitry@galenko.su>
# License CC BY-SA v.4.0 <https://creativecommons.org/licenses/by-sa/4.0/>
#
# Don't forgot define $aws ;)
#

## Our bicycles
JSONToList () {
	grep -vE '(\[|\])' | sed 's/"//g;s/,//g;/^\s*$/d'| awk '{print $1}'
}
AutoChangeInstanceCByID () {
	ID="${1}"
	tminload="${2}"
	tmaxload="${3}"
	elbtarget="${4}"
	preload="${5}"
	chaction=0;
	g=5 # Family generation
	ptype="c${g}.9xlarge" # Preload to this type
	cload=`CWGetAwgCPUByID ${ID}`
	# Time to upgrade?
	if [ ${cload} -gt ${tmaxload} -o -n "${preload}" ]; then
		echo "[${ID}] High load detected: ${cload}%"
		ctype=`EC2GetTypeByID ${ID}`
		chaction=1;
		if [ -n "${preload}" ]; then
			ttype="${ptype}"
		else
			case ${ctype} in 
				c*.large) 
					ttype="c${g}.xlarge"
				;;
				c*.xlarge) 
					ttype="c${g}.2xlarge"
				;;
				c*.2xlarge) 
					ttype="c${g}.4xlarge"
				;;
				c*.4xlarge) 
			                ttype="c${g}.9xlarge"
				;;
				c*.9xlarge)
			                ttype="c${g}.18xlarge"
				;;
				c*.18xlarge)
					chaction=0;
				;;
				c*.8xlarge)
					# Look like time to upgrade to c5.9xlarge
					ttype="c5.9xlarge"
				;;
				*)
					ttype="c${g}.large"
				;;
			esac
		fi
		if [ "${ctype}" = "${ttype}" ]; then
			chaction=0;
		fi
	fi
	# Time to downgrade?
#	if [ ${cload} -lt ${tminload} -a ${cload} -ne 0 ]; then #ToDo fix zeroload
	if [ ${cload} -lt ${tminload} -a ${cload} ]; then
		echo "[${ID}] Low load detected: ${cload}%"
		ctype=`EC2GetTypeByID ${ID}`
		chaction=1;
		case ${ctype} in 
			c*.large) 
				chaction=0;
			;;
			c*.xlarge) 
				if [ -n "${preload}" ]; then
					chaction=0
				else
					ttype="c${g}.large"
				fi
			;;
			c*.2xlarge) 
				if [ -n "${preload}" ]; then
					chaction=0
				else
					ttype="c${g}.xlarge"
				fi
			;;
			c*.4xlarge)
				if [ -n "${preload}" ]; then
					chaction=0
				else 
					ttype="c${g}.2xlarge"
				fi
			;;
			c*.9xlarge)
				if [ -n "${preload}" ]; then
					chaction=0
				else 
					ttype="c${g}.4xlarge"
				fi
			;;
			c*.18xlarge)
				if [ -n "${preload}" ]; then
					chaction=0
				else 
					ttype="c${g}.9xlarge"
				fi
			;;
			*)
				ttype="c${g}.large"
			;;
		esac
		if [ "${ctype}" = "${ttype}" ]; then
			chaction=0;
		fi
	fi
	# Showtime!
	if [ ${chaction} -eq 1 ]; then
		echo -en "[${ID}] Unregistring..."
		ELBUnRegTargetByID ${elbtarget} ${ID}
		sleep 10;
		echo -en "\n[${ID}] Stoping instance"
		EC2StopByID ${ID}
        echo -en "\n[${ID}] Looking for ENA support..."
        ENASupport=`EC2GetENASupport ${ID}`
        if [ "${ENASupport}" == "false" ]; then
            echo -en "\n[${ID}] Enabling ENA support..."
            EC2EnableENA ${ID}
        fi
		echo -en "\n[${ID}] Changing instance type to ${ttype}..."
		EC2ChangeTypeByID ${ID} ${ttype}
		sleep 3;
		echo -en "\n[${ID}] Starting instance"
		EC2StartByID ${ID}
		sstat='nope'
		echo -en "\n[${ID}] Waiting for bootup instance"
		while [ "${sstat}" != "ok" ]; do
			sstat=`EC2CheckSystemStatusByID ${ID}`
			EC2StartByID ${ID}
			echo -en '.'
			sleep 2;
		done
		echo -e "\n[${ID}] Registring..."
		ELBRegTargetByID ${elbtarget} ${ID}
		echo "---------------------------------------------------"
		chaction=0;
	else
		echo "[${ID}] Not need any action (CPU ${cload}%)."
	fi
}
## EC2
EC2GetENASupport () {
	${aws} ec2 describe-instances --instance-ids ${1} --query 'Reservations[].Instances[].EnaSupport'| JSONToList
}
EC2EnableENA () {
	${aws} ec2 modify-instance-attribute --instance-id ${1} --ena-support| JSONToList
}
EC2GetInstances () {
	${aws} ec2 describe-instances --query 'Reservations[].Instances[].InstanceId' --filters Name=tag:Name,Values=${1}| JSONToList
}
EC2GetTypeByID () {
	${aws} ec2 describe-instances --query 'Reservations[].Instances[].InstanceType' --filters Name=instance-id,Values=${1}| JSONToList
}
EC2ForceStopByID () {
	rcode='999'
	while [ ${rcode} -ne 80 ]; do
		rcode=`${aws} ec2 stop-instances --force --instance-ids ${1} |jq '.StoppingInstances[].CurrentState.Code'`
		echo -en '.'
		sleep 2;
	done
}
EC2StopByID () {
	rcode='999'

		while [ ${rcode} -ne 80 ]; do
			rcode=`${aws} ec2 stop-instances --instance-ids ${1} |jq '.StoppingInstances[].CurrentState.Code'`
			if [ -n "$rcode" -a -z "${rcode//[0-9]}" ]; then
				echo -en '.'
				sleep 2;
			else
				echo "ERR: ${FUNCNAME[0]} Int status expected. Action failed!"
				exit 65;		
			fi
		done
}
EC2StartByID () {
	rcode='999'
		while [ ${rcode} -ne 16 ]; do
			rcode=`${aws} ec2 start-instances --instance-ids ${1}|jq '.StartingInstances[].CurrentState.Code'`
			if [ -n "$rcode" -a -z "${rcode//[0-9]}" ]; then
				echo -en '.'
				sleep 2;
			else
				echo "ERR: ${FUNCNAME[0]} Int status expected. Action failed!"
				exit 65;		
			fi
		done
}
EC2CheckSystemStatusByID () {
	${aws} ec2 describe-instance-status --instance-ids ${1}|jq '.InstanceStatuses[].InstanceStatus.Status'|JSONToList
}
EC2ChangeTypeByID () {
	${aws} ec2 modify-instance-attribute --instance-id ${1} --instance-type ${2}
}
## CloudWatch
CWGetAwgCPUByID () {
	EndTime=`date -u --iso-8601=minutes  --date="-2 minutes"`
	StartTime=`date -u --iso-8601=minutes --date="-10 minutes"`
	result=`${aws} cloudwatch get-metric-statistics --namespace AWS/EC2 --metric-name CPUUtilization --dimensions Name=InstanceId,Value=${1} \
		--period 60 --statistics Average \
		--end-time=${EndTime} \
		--start-time=${StartTime} |\
		jq '.Datapoints[].Average'|head -1|cut -f1 -d.`
	if [[ ${result} =~ ^-?[0-9]+$ ]]; then
		echo "${result}"
	else
		echo "0"		
	fi
}
## Application ELB (aka ELB v.2)
ELBUnRegTargetByID () {
	${aws} elbv2 deregister-targets --target-group-arn ${1} --targets Id=${2}
}
ELBRegTargetByID () {
	${aws} elbv2 register-targets --target-group-arn ${1} --targets Id=${2}
}

## Decode encrypted error messages
AWSDecodeErr () {
	${aws} sts decode-authorization-message --encoded-message "$1" | jq '.["DecodedMessage"]' |sed 's/\\"/"/g;s/^"//;s/"$//' |jq
}

## Database Migration Service
DMSGetTaskStatusByARN ()
{
	${aws} dms describe-replication-tasks --filters "Name=replication-task-arn,Values=${1}" | jq '.ReplicationTasks[].Status' | sed 's/\\"/"/g;s/^"//;s/"$//'
}
DMSRestartTaskByARN ()
{
	${aws} dms start-replication-task --replication-task-arn ${1} --start-replication-task-type "reload-target" | jq '.ReplicationTask.Status' | sed 's/\\"/"/g;s/^"//;s/"$//'
}
