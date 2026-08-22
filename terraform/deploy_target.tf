# Upstream: network.tf (aws_eip.web.public_ip), variables.tf (ssh_private_key_path).
# Downstream: nothing in Terraform — this is the bridge into `deploy/deploy.sh`, which
# sources the file this writes to pick up host/key without manual copy-pasting.

resource "local_file" "deploy_env" {
  filename = "${path.module}/../deploy/.env"

  content = templatefile("${path.module}/templates/deploy.env.tpl", {
    public_ip            = aws_eip.web.public_ip
    ssh_private_key_path = var.ssh_private_key_path
  })
}
