# Upstream: compute.tf (aws_instance.web), network.tf (aws_eip_association.web), variables.tf.
# Downstream: nothing — terminal step, runs as part of `terraform apply`.
#
#   null_resource.provision (OS setup — scripts/provision.sh, re-runs on content change)
#           │
#           ▼
#   null_resource.deploy_app (optional: binary + frontend dist, only when both
#                              app_binary_local_path and frontend_dist_local_dir are set)

resource "null_resource" "provision" {
  depends_on = [aws_eip_association.web]

  # Re-runs whenever the script's content changes, not just on instance replacement.
  triggers = {
    script_sha = filesha256("${path.module}/scripts/provision.sh")
  }

  connection {
    type        = "ssh"
    host        = aws_eip.web.public_ip
    user        = "root"
    private_key = file(var.ssh_private_key_path)
    timeout     = "5m"
  }

  provisioner "file" {
    source      = "${path.module}/scripts/provision.sh"
    destination = "/root/provision.sh"
  }

  provisioner "remote-exec" {
    inline = [
      "chmod +x /root/provision.sh",
      "/root/provision.sh",
    ]
  }
}

# Neither exists yet this early in the project — set both vars (in terraform.tfvars,
# not tracked) once a real build produces them, then `terraform apply` syncs and
# restarts the service. Re-syncs whenever the binary or any dist file changes.
resource "null_resource" "deploy_app" {
  count      = (var.app_binary_local_path != "" && var.frontend_dist_local_dir != "") ? 1 : 0
  depends_on = [null_resource.provision]

  triggers = {
    binary_sha = filesha256(var.app_binary_local_path)
    dist_sha = sha1(join("", [
      for f in sort(fileset(var.frontend_dist_local_dir, "**")) :
      filesha1("${var.frontend_dist_local_dir}/${f}")
    ]))
  }

  connection {
    type        = "ssh"
    host        = aws_eip.web.public_ip
    user        = "root"
    private_key = file(var.ssh_private_key_path)
    timeout     = "5m"
  }

  # Cleared first so removed files don't linger (file provisioner only adds/overwrites).
  provisioner "remote-exec" {
    inline = ["mkdir -p /opt/temperingworks/static", "rm -rf /opt/temperingworks/static/*"]
  }

  provisioner "file" {
    source      = var.app_binary_local_path
    destination = "/opt/temperingworks/bin/temperingworks-server"
  }

  provisioner "file" {
    source      = "${var.frontend_dist_local_dir}/"
    destination = "/opt/temperingworks/static"
  }

  provisioner "remote-exec" {
    inline = [
      "chown temperingworks:temperingworks /opt/temperingworks/bin/temperingworks-server",
      "chmod 755 /opt/temperingworks/bin/temperingworks-server",
      "rc-service temperingworks-server status >/dev/null 2>&1 && rc-service temperingworks-server restart || rc-service temperingworks-server start",
    ]
  }
}
