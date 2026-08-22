# Upstream: network.tf, deploy_target.tf. Downstream: nothing — terminal node, printed to the operator.

output "public_ip" {
  description = "Elastic IP of the instance. Point temperingworks.com's A record (and www) here manually in the GoDaddy dashboard — see README.md."
  value       = aws_eip.web.public_ip
}

output "ssh_command" {
  description = "Ready-to-run SSH command (root — see compute.tf for why)."
  value       = "ssh -i ${var.ssh_private_key_path} root@${aws_eip.web.public_ip}"
}

output "next_step" {
  description = "What to run after `terraform apply` finishes."
  value       = "cd ../deploy && ./deploy.sh"
}
