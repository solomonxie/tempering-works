# Upstream: ec2.tf (aws_instance.web).
# Downstream: inventory.tf and outputs.tf both read the resulting public_ip; this is also
# the address manually pointed to from GoDaddy (see terraform/README.md).

resource "aws_eip" "web" {
  domain = "vpc" # provider v5: replaces the removed legacy `vpc = true` argument

  tags = {
    Name = "${var.project_prefix}-eip"
  }
}

resource "aws_eip_association" "web" {
  instance_id   = aws_instance.web.id
  allocation_id = aws_eip.web.id
}
