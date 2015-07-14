static int cma_req_handler(struct ib_cm_id *cm_id, struct ib_cm_event *ib_event)
{
	struct rdma_id_private *listen_id, *conn_id;
	struct rdma_cm_event event;
	int offset, ret;

	listen_id = cm_id->context;
	if (cma_disable_callback(listen_id, CMA_LISTEN))
		return -ECONNABORTED;

	memset(&event, 0, sizeof event);
	offset = cma_user_data_offset(listen_id->id.ps);
	event.event = RDMA_CM_EVENT_CONNECT_REQUEST;
	if (cma_is_ud_ps(listen_id->id.ps)) {
		conn_id = cma_new_udp_id(&listen_id->id, ib_event);
		event.param.ud.private_data = ib_event->private_data + offset;
		event.param.ud.private_data_len =
				IB_CM_SIDR_REQ_PRIVATE_DATA_SIZE - offset;
	} else {
		conn_id = cma_new_conn_id(&listen_id->id, ib_event);
		cma_set_req_event_data(&event, &ib_event->param.req_rcvd,
				       ib_event->private_data, offset);
	}
	if (!conn_id) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock_nested(&conn_id->handler_mutex, SINGLE_DEPTH_NESTING);
	mutex_lock(&lock);
	ret = cma_acquire_dev(conn_id);
	mutex_unlock(&lock);
	if (ret)
		goto release_conn_id;

	conn_id->cm_id.ib = cm_id;
	cm_id->context = conn_id;
	cm_id->cm_handler = cma_ib_handler;

	ret = conn_id->id.event_handler(&conn_id->id, &event);
	if (!ret) {
		/*
		 * Acquire mutex to prevent user executing rdma_destroy_id()
		 * while we're accessing the cm_id.
		 */
		mutex_lock(&lock);
		if (cma_comp(conn_id, CMA_CONNECT) &&
		    !cma_is_ud_ps(conn_id->id.ps))
			ib_send_cm_mra(cm_id, CMA_CM_MRA_SETTING, NULL, 0);
		mutex_unlock(&lock);
		mutex_unlock(&conn_id->handler_mutex);
		goto out;
	}

	/* Destroy the CM ID by returning a non-zero value. */
	conn_id->cm_id.ib = NULL;

release_conn_id:
	cma_exch(conn_id, CMA_DESTROYING);
	mutex_unlock(&conn_id->handler_mutex);
	rdma_destroy_id(&conn_id->id);

out:
	mutex_unlock(&listen_id->handler_mutex);
	return ret;
}

static int iw_conn_req_handler(struct iw_cm_id *cm_id,
			       struct iw_cm_event *iw_event)
{
	struct rdma_cm_id *new_cm_id;
	struct rdma_id_private *listen_id, *conn_id;
	struct sockaddr_in *sin;
	struct net_device *dev = NULL;
	struct rdma_cm_event event;
	int ret;
	struct ib_device_attr attr;

	listen_id = cm_id->context;
	if (cma_disable_callback(listen_id, CMA_LISTEN))
		return -ECONNABORTED;

	/* Create a new RDMA id for the new IW CM ID */
	new_cm_id = rdma_create_id(listen_id->id.event_handler,
				   listen_id->id.context,
				   RDMA_PS_TCP);
	if (IS_ERR(new_cm_id)) {
		ret = -ENOMEM;
		goto out;
	}
	conn_id = container_of(new_cm_id, struct rdma_id_private, id);
	mutex_lock_nested(&conn_id->handler_mutex, SINGLE_DEPTH_NESTING);
	conn_id->state = CMA_CONNECT;

	dev = ip_dev_find(&init_net, iw_event->local_addr.sin_addr.s_addr);
	if (!dev) {
		ret = -EADDRNOTAVAIL;
		mutex_unlock(&conn_id->handler_mutex);
		rdma_destroy_id(new_cm_id);
		goto out;
	}
	ret = rdma_copy_addr(&conn_id->id.route.addr.dev_addr, dev, NULL);
	if (ret) {
		mutex_unlock(&conn_id->handler_mutex);
		rdma_destroy_id(new_cm_id);
		goto out;
	}

	mutex_lock(&lock);
	ret = cma_acquire_dev(conn_id);
	mutex_unlock(&lock);
	if (ret) {
		mutex_unlock(&conn_id->handler_mutex);
		rdma_destroy_id(new_cm_id);
		goto out;
	}

	conn_id->cm_id.iw = cm_id;
	cm_id->context = conn_id;
	cm_id->cm_handler = cma_iw_handler;

	sin = (struct sockaddr_in *) &new_cm_id->route.addr.src_addr;
	*sin = iw_event->local_addr;
	sin = (struct sockaddr_in *) &new_cm_id->route.addr.dst_addr;
	*sin = iw_event->remote_addr;

	ret = ib_query_device(conn_id->id.device, &attr);
	if (ret) {
		mutex_unlock(&conn_id->handler_mutex);
		rdma_destroy_id(new_cm_id);
		goto out;
	}

	memset(&event, 0, sizeof event);
	event.event = RDMA_CM_EVENT_CONNECT_REQUEST;
	event.param.conn.private_data = iw_event->private_data;
	event.param.conn.private_data_len = iw_event->private_data_len;
	event.param.conn.initiator_depth = attr.max_qp_init_rd_atom;
	event.param.conn.responder_resources = attr.max_qp_rd_atom;
	ret = conn_id->id.event_handler(&conn_id->id, &event);
	if (ret) {
		/* User wants to destroy the CM ID */
		conn_id->cm_id.iw = NULL;
		cma_exch(conn_id, CMA_DESTROYING);
		mutex_unlock(&conn_id->handler_mutex);
		rdma_destroy_id(&conn_id->id);
		goto out;
	}

	mutex_unlock(&conn_id->handler_mutex);

out:
	if (dev)
		dev_put(dev);
	mutex_unlock(&listen_id->handler_mutex);
	return ret;
}

static int cm_sidr_req_handler(struct cm_work *work)
{
	struct ib_cm_id *cm_id;
	struct cm_id_private *cm_id_priv, *cur_cm_id_priv;
	struct cm_sidr_req_msg *sidr_req_msg;
	struct ib_wc *wc;
	unsigned long flags;

	cm_id = ib_create_cm_id(work->port->cm_dev->device, NULL, NULL);
	if (IS_ERR(cm_id))
		return PTR_ERR(cm_id);
	cm_id_priv = container_of(cm_id, struct cm_id_private, id);

	/* Record SGID/SLID and request ID for lookup. */
	sidr_req_msg = (struct cm_sidr_req_msg *)
				work->mad_recv_wc->recv_buf.mad;
	wc = work->mad_recv_wc->wc;
	cm_id_priv->av.dgid.global.subnet_prefix = cpu_to_be64(wc->slid);
	cm_id_priv->av.dgid.global.interface_id = 0;
	cm_init_av_for_response(work->port, work->mad_recv_wc->wc,
				work->mad_recv_wc->recv_buf.grh,
				&cm_id_priv->av);
	cm_id_priv->id.remote_id = sidr_req_msg->request_id;
	cm_id_priv->id.state = IB_CM_SIDR_REQ_RCVD;
	cm_id_priv->tid = sidr_req_msg->hdr.tid;
	atomic_inc(&cm_id_priv->work_count);

	spin_lock_irqsave(&cm.lock, flags);
	cur_cm_id_priv = cm_insert_remote_sidr(cm_id_priv);
	if (cur_cm_id_priv) {
		spin_unlock_irqrestore(&cm.lock, flags);
		goto out; /* Duplicate message. */
	}
	cur_cm_id_priv = cm_find_listen(cm_id->device,
					sidr_req_msg->service_id,
					sidr_req_msg->private_data);
	if (!cur_cm_id_priv) {
		rb_erase(&cm_id_priv->sidr_id_node, &cm.remote_sidr_table);
		spin_unlock_irqrestore(&cm.lock, flags);
		/* todo: reply with no match */
		goto out; /* No match. */
	}
	atomic_inc(&cur_cm_id_priv->refcount);
	spin_unlock_irqrestore(&cm.lock, flags);

	cm_id_priv->id.cm_handler = cur_cm_id_priv->id.cm_handler;
	cm_id_priv->id.context = cur_cm_id_priv->id.context;
	cm_id_priv->id.service_id = sidr_req_msg->service_id;
	cm_id_priv->id.service_mask = __constant_cpu_to_be64(~0ULL);

	cm_format_sidr_req_event(work, &cur_cm_id_priv->id);
	cm_process_work(cm_id_priv, work);
	cm_deref_id(cur_cm_id_priv);
	return 0;
out:
	ib_destroy_cm_id(&cm_id_priv->id);
	return -EINVAL;
}
