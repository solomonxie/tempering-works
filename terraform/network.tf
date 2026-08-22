# Upstream: variables.tf, compute.tf (default vpc data, and the instance the EIP attaches to).
# Downstream: compute.tf (security group id attached to the instance), ansible_inventory.tf
# and outputs.tf (both read the EIP's public_ip).

resource "aws_security_group" "web" {
  name        = "${var.project_prefix}-web-sg"
  description = "SSH from a single CIDR, app port from anywhere"
  vpc_id      = data.aws_vpc.default.id

  ingress {
    description = "SSH"
    from_port   = 22
    to_port     = 22
    protocol    = "tcp"
    cidr_blocks = [var.ssh_ingress_cidr]
  }

  ingress {
    description = "App HTTP/TCP"
    from_port   = var.app_http_port
    to_port     = var.app_http_port
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]
  }

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }

  tags = {
    Name = "${var.project_prefix}-web-sg"
  }
}

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
