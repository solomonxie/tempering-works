# Upstream: variables.tf, network.tf (security group).
# Downstream: network.tf's EIP, provisioning.tf, outputs.tf.
#
#   data.aws_vpc.default ──> data.aws_subnets.default ──┐
#   data.aws_ami.alpine ─────────────────────────────────┼──> aws_instance.web
#   data.aws_key_pair.existing ───────────────────────────┘

data "aws_vpc" "default" {
  default = true
}

data "aws_subnets" "default" {
  filter {
    name   = "vpc-id"
    values = [data.aws_vpc.default.id]
  }
}

# Official Alpine Linux AMIs, published by Alpine's own AWS account.
# Pinned to Alpine 3.24, the "tiny" (tiny-cloud) bootstrap variant — NOT "cloudinit"
# (Alpine has no cloud-init support at all; tiny-cloud only accepts shell user_data).
# Verify before first apply: aws ec2 describe-images --owners 538276064493 \
#   --filters "Name=name,Values=alpine-3.24.*-aarch64-*-tiny-*" "Name=architecture,Values=arm64"
data "aws_ami" "alpine" {
  most_recent = true
  owners      = ["538276064493"] # Alpine Linux — never wildcard this

  filter {
    name   = "name"
    values = ["alpine-3.24.*-aarch64-*-tiny-*"]
  }

  filter {
    name   = "architecture"
    values = ["arm64"]
  }

  filter {
    name   = "virtualization-type"
    values = ["hvm"]
  }

  filter {
    name   = "root-device-type"
    values = ["ebs"]
  }
}

# Must already exist in AWS — this plan never creates a key pair.
data "aws_key_pair" "existing" {
  key_name = var.key_pair_name
}

resource "aws_instance" "web" {
  ami                    = data.aws_ami.alpine.id
  instance_type          = var.instance_type
  subnet_id              = element(sort(data.aws_subnets.default.ids), 0)
  vpc_security_group_ids = [aws_security_group.web.id]
  key_name               = data.aws_key_pair.existing.key_name

  associate_public_ip_address = true

  metadata_options {
    http_tokens                 = "required" # IMDSv2 only
    http_endpoint               = "enabled"
    http_put_response_hop_limit = 1
  }

  root_block_device {
    volume_type = "gp3"
    volume_size = var.root_volume_size_gb
    encrypted   = true
  }

  # tiny-cloud implements a subset of cloud-init's #cloud-config format (users/packages/
  # runcmd), not full cloud-init. Alpine's cloud AMIs (3.15+) ship without sudo/doas, so
  # the default "alpine" user can't escalate to root — instead this sets root's own
  # ssh_authorized_keys directly, so provisioning.tf's provisioners connect as root.
  # Verify at first boot: if root SSH doesn't come up, use EC2 Serial Console to recover.
  user_data = <<-EOF
    #cloud-config
    users:
      - name: root
        ssh_authorized_keys:
          - ${trimspace(file(var.ssh_public_key_path))}
  EOF

  tags = {
    Name = "${var.project_prefix}-web"
  }
}
