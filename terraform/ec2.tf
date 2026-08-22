# Upstream: variables.tf, data.tf (ami/subnet/key), security_group.tf.
# Downstream: eip.tf (associates to this instance), inventory.tf (reads its data via the eip).

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

  # tiny-cloud only understands shell user_data (must start with #!) — Alpine has no
  # cloud-init support, so a #cloud-config YAML block here would silently do nothing.
  # Best-effort: Ansible's common role has a raw-module fallback for this in case apk
  # mirrors aren't reachable yet at this point in boot.
  user_data = <<-EOF
    #!/bin/sh
    apk update
    apk add --no-cache python3
  EOF

  tags = {
    Name = "${var.project_prefix}-web"
  }
}
