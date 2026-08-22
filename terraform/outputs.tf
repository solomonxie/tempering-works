# Upstream: network.tf, provisioning.tf. Downstream: nothing — terminal node, printed to the operator.

output "instance_id" {
  description = "EC2 instance id, used by the Makefile's start-server/stop-server targets."
  value       = aws_instance.web.id
}

output "public_ip" {
  description = "Elastic IP of the instance. Point temperingworks.com's A record (and www) here manually in the GoDaddy dashboard — see README.md."
  value       = aws_eip.web.public_ip
}

output "ssh_command" {
  description = "Ready-to-run SSH command (root — see compute.tf for why)."
  value       = "ssh -i ${var.ssh_private_key_path} root@${aws_eip.web.public_ip}"
}
