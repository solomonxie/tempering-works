# Upstream: variables.tf (ssh_ingress_cidr, app_http_port, project_prefix), data.tf (default vpc).
# Downstream: ec2.tf (attaches this security group to the instance).

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
