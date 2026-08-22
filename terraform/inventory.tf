# Upstream: eip.tf (aws_eip.web.public_ip), variables.tf (ssh_private_key_path).
# Downstream: nothing in Terraform — this is the bridge into `ansible-playbook`, which
# reads the file this writes.

resource "local_file" "ansible_inventory" {
  filename = "${path.module}/../ansible/inventory/hosts.ini"

  content = templatefile("${path.module}/templates/hosts.ini.tpl", {
    public_ip            = aws_eip.web.public_ip
    ssh_private_key_path = var.ssh_private_key_path
  })
}
