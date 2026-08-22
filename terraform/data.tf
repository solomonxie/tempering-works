# Upstream: variables.tf (var.key_pair_name).
# Downstream: security_group.tf (vpc id), ec2.tf (ami id, subnet id, key name).
#
#   aws_vpc.default ──> aws_subnets.default
#   aws_ami.alpine          (independent lookup)
#   aws_key_pair.existing   (independent lookup)

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
