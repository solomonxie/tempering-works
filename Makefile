export
EC2_ID := "$$(terraform -chdir=terraform output -raw instance_id)"


dryrun-infra:
	terraform -chdir=terraform init
	terraform -chdir=terraform plan

deploy-infra:
	terraform -chdir=terraform apply

destroy-infra:
	terraform -chdir=terraform destroy


deploy-software:
	cd deploy && ./deploy.sh


start-server:
	aws ec2 start-instances --instance-ids ${EC2_ID}

stop-server:
	aws ec2 stop-instances --instance-ids ${EC2_ID}
