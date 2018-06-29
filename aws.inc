#!/bin/bash
#
# Author Dmitry A. Galenko <dmitry@galenko.su>
# License CC BY-SA v.4.0 <https://creativecommons.org/licenses/by-sa/4.0/>
#
# Don't forgot define $aws ;)
#
# 20180729:
#   AutoChangeInstanceCByID - Autoupgrade any "c" family instances to "c5" family.


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
    ptype="c${g}.9xlarge"
	cload=`CWGetAwgCPUByID ${ID}`
	# Time to upgrade?
	if [ ${cload} -gt ${tmaxload} ] || [ ! -z "${preload}" ]; then
		echo "[${ID}] High load detected: ${cload}%"
		ctype=`EC2GetTypeByID ${ID}`
		chaction=1;
		case ${ctype} in 
			c*.large) 
				if [ ! -z "${preload}" ]; then
					ttype="${ptype}"
				else
					ttype="c${g}.xlarge"
				fi
			;;
			c*.xlarge) 
				if [ ! -z "${preload}" ]; then
					ttype="${ptype}"
				else
					ttype="c${g}.2xlarge"
				fi
			;;
			c*.2xlarge) 
				if [ ! -z "${preload}" ]; then
					ttype="${ptype}"
				else
					ttype="c${g}.4xlarge"
				fi
			;;
			c*.4xlarge) 
				if [ ! -z "${preload}" ]; then
					ttype="${ptype}"
				else
                    ttype="c${g}.9xlarge"
                fi
			;;
			c*.8xlarge)
                # Look like time to upgrade to c5.9xlarge
				ttype="c${g}.9xlarge"
			;;
			c*.9xlarge)
				ttype="c${g}.18xlarge"
			;;
			c*.18xlarge)
				chaction=0;
			;;
			*)
				ttype="c${g}.large"
			;;
		esac
	fi
	# Time to downgrade?
	if [ ${cload} -lt ${tminload} ] && [ ${cload} -ne 0 ]; then
		echo "[${ID}] Low load detected: ${cload}%"
		ctype=`EC2GetTypeByID ${ID}`
		chaction=1;
		case ${ctype} in 
			c*.large) 
				chaction=0;
			;;
			c*.xlarge) 
				if [ ! -z "${preload}" ]; then
					chaction=0
				else
					ttype="c${g}.large"
				fi
			;;
			c*.2xlarge) 
				if [ ! -z "${preload}" ]; then
					chaction=0
				else
					ttype="c${g}.xlarge"
				fi
			;;
			c*.4xlarge)
				if [ ! -z "${preload}" ]; then
					chaction=0
				else 
					ttype="c${g}.2xlarge"
				fi
			;;
			c*.9xlarge)
				if [ ! -z "${preload}" ]; then
					chaction=0
				else 
					ttype="c${g}.4xlarge"
				fi
            ;;
			c*.18xlarge)
				if [ ! -z "${preload}" ]; then
					chaction=0
				else 
					ttype="c${g}.9xlarge"
				fi
            ;;
			*)
				ttype="c${g}.large"
			;;
		esac
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
			sleep 1;
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
		sleep 1;
	done
}
EC2StopByID () {
	rcode='999'
	while [ ${rcode} -ne 80 ]; do
		rcode=`${aws} ec2 stop-instances --instance-ids ${1} |jq '.StoppingInstances[].CurrentState.Code'`
		echo -en '.'
		sleep 1;
	done
}
EC2StartByID () {
	rcode='999'
        while [ ${rcode} -ne 16 ]; do
		rcode=`${aws} ec2 start-instances --instance-ids ${1}|jq '.StartingInstances[].CurrentState.Code'`
		echo -en '.'
		sleep 1;
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
	${aws} cloudwatch get-metric-statistics --namespace AWS/EC2 --metric-name CPUUtilization --dimensions Name=InstanceId,Value=${1} \
		--period 60 --statistics Average \
		--end-time=${EndTime} \
		--start-time=${StartTime} |\
		jq '.Datapoints[].Average'|head -1|cut -f1 -d.
}
## Application ELB (aka ELB v.2)
ELBUnRegTargetByID () {
	${aws} elbv2 deregister-targets --target-group-arn ${1} --targets Id=${2}
}
ELBRegTargetByID () {
	${aws} elbv2 register-targets --target-group-arn ${1} --targets Id=${2}
}

AWSDecodeErr () {
	${aws} sts decode-authorization-message --encoded-message "$1" | jq '.["DecodedMessage"]' |sed 's/\\"/"/g' |sed 's/^"//' |sed 's/"$//' |jq
}
