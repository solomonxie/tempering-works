# Terraform

Provisions a single EC2 instance (Alpine Linux, ARM64, `t4g.small`, `tiny`/tiny-cloud
bootstrap) in the account's default VPC, and configures its OS entirely as part of
`terraform apply` — no separate deploy step, no Ansible, no Python on the target. File
order on disk doesn't matter — reference order does; see each file's header comment for
its place in the graph below.

```
terraform apply
  ├─ providers.tf        → aws + null provider setup
  ├─ variables.tf         → resolve inputs (key pair name/paths/CIDR from terraform.tfvars)
  ├─ compute.tf             → default VPC/subnet + Alpine 3.24 tiny AMI + existing key pair
  │                            lookups, then the instance itself (root's SSH key is set via
  │                            user_data — Alpine ships without sudo/doas)
  ├─ network.tf              → security group (SSH from your CIDR, app port public) + EIP
  ├─ provisioning.tf           → pushes scripts/provision.sh over SSH and runs it (base
  │                              packages, sshd hardening, C10K sysctl, toolchain, logging,
  │                              app service); if app_binary_local_path and
  │                              frontend_dist_local_dir are set, also syncs the app + restarts it
  └─ outputs.tf                  → prints instance_id, public_ip, ssh command
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

Re-running `apply` re-provisions the OS if `scripts/provision.sh` changed, and re-syncs
the app if the binary or frontend dist changed — both are driven by content-hash
triggers in `provisioning.tf`, not just instance replacement.
