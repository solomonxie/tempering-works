# Terraform

Provisions a single EC2 instance (Alpine Linux, ARM64, `t4g.small`, `tiny`/tiny-cloud
bootstrap) in the account's default VPC, plus the Elastic IP and security group it needs,
and hands off to `../deploy/`. File order on disk doesn't matter — reference order
does; see each file's header comment for its place in the graph below.

```
terraform apply
  ├─ providers.tf        → aws + local provider setup
  ├─ variables.tf         → resolve inputs (key pair name/paths/CIDR from terraform.tfvars)
  ├─ compute.tf             → default VPC/subnet + Alpine 3.24 tiny AMI + existing key pair
  │                            lookups, then the instance itself (root's SSH key is set via
  │                            user_data — Alpine ships without sudo/doas)
  ├─ network.tf              → security group (SSH from your CIDR, app port public) + EIP
  ├─ deploy_target.tf          → writes deploy/.env from the EIP (bridge step)
  └─ outputs.tf                  → prints public_ip, ssh command, next-step hint
        │
        ▼
deploy/deploy.sh   (shell scripts over SSH — no Ansible, no Python on the target; see ../deploy/README.md)
        │
        ▼
GoDaddy dashboard (manual)   (point temperingworks.com's A record + www at `public_ip`)
```

## GoDaddy DNS is manual, not automated

Since May 2024, GoDaddy restricts its Domain/DNS Management API to accounts with 10+
domains or an active Discount Domain Club Premier plan — a normal single-domain account
gets a hard access-denied, and the community Terraform providers for GoDaddy stopped
being maintained around the same time. Rather than build against an API most accounts
can't call, DNS is a manual step: after `apply`, copy the `public_ip` output and update
temperingworks.com's `A` record (and `www`) in the GoDaddy dashboard.

## Setup

```
cp terraform.tfvars.example terraform.tfvars   # fill in real values, never commit this file
terraform init
terraform plan
terraform apply
```
