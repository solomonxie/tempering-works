# Upstream: none (pure input declarations).
# Downstream: read by compute.tf, network.tf, deploy_target.tf, outputs.tf.
# Real values for the no-default vars below come only from a gitignored terraform.tfvars —
# never hardcode a real key pair name/path or CIDR here or in any tracked file.

variable "aws_region" {
  description = "AWS region to provision into."
  type        = string
  default     = "us-east-1"
}

variable "project_prefix" {
  description = "Prefix applied to resource names/tags."
  type        = string
  default     = "temperingworks"
}

variable "instance_type" {
  description = "EC2 instance type (must be a Graviton/ARM64 family, e.g. t4g.*)."
  type        = string
  default     = "t4g.small"
}

variable "key_pair_name" {
  description = "Name of an EXISTING AWS EC2 key pair to use for SSH access. Set in terraform.tfvars, not here."
  type        = string
}

variable "ssh_private_key_path" {
  description = "Local path to the private key matching key_pair_name, used only to render provision/.env. Set in terraform.tfvars, not here."
  type        = string
}

variable "ssh_public_key_path" {
  description = "Local path to the public key matching key_pair_name. Its content is embedded in user_data to set root's authorized_keys (tiny-cloud has no sudo/doas by default, so provisioning connects as root directly). Set in terraform.tfvars, not here."
  type        = string
}

variable "ssh_ingress_cidr" {
  description = "CIDR allowed to SSH into the instance (e.g. your.ip.addr/32). Set in terraform.tfvars, not here."
  type        = string
}

variable "root_volume_size_gb" {
  description = "Root EBS volume size in GB. Sized for on-box C++ compilation, not just the OS."
  type        = number
  default     = 20
}

variable "app_http_port" {
  description = "TCP port the C++ server listens on and that the security group opens publicly."
  type        = number
  default     = 8080
}
