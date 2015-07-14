after patch:
static gboolean
ves_icall_System_Array_FastCopy (MonoArray *source, int source_idx, MonoArray* dest, int dest_idx, int length)
{
	int element_size;
	void * dest_addr;
	void * source_addr;
	MonoClass *src_class;
	MonoClass *dest_class;

	MONO_ARCH_SAVE_REGS;

	if (source->obj.vtable->klass->rank != dest->obj.vtable->klass->rank)
		return FALSE;

	if (source->bounds || dest->bounds)
		return FALSE;

	/* there's no integer overflow since mono_array_length returns an unsigned integer */
	if ((dest_idx + length > mono_array_length (dest)) ||
		(source_idx + length > mono_array_length (source)))
		return FALSE;

	src_class = source->obj.vtable->klass->element_class;
	dest_class = dest->obj.vtable->klass->element_class;

	/*
	 * Handle common cases.
	 */

	/* Case1: object[] -> valuetype[] (ArrayList::ToArray) */
	if (src_class == mono_defaults.object_class && dest_class->valuetype) {
		// FIXME: This is racy
		return FALSE;
		/*
		  int i;
		int has_refs = dest_class->has_references;
		for (i = source_idx; i < source_idx + length; ++i) {
			MonoObject *elem = mono_array_get (source, MonoObject*, i);
			if (elem && !mono_object_isinst (elem, dest_class))
				return FALSE;
		}
		element_size = mono_array_element_size (dest->obj.vtable->klass);
		memset (mono_array_addr_with_size (dest, element_size, dest_idx), 0, element_size * length);
		for (i = 0; i < length; ++i) {
			MonoObject *elem = mono_array_get (source, MonoObject*, source_idx + i);
			void *addr = mono_array_addr_with_size (dest, element_size, dest_idx + i);
			if (!elem)
				continue;
			if (has_refs)
				mono_value_copy (addr, (char *)elem + sizeof (MonoObject), dest_class);
			else
				memcpy (addr, (char *)elem + sizeof (MonoObject), element_size);
		}
		return TRUE;
		*/
	}

	/* Check if we're copying a char[] <==> (u)short[] */
	if (src_class != dest_class) {
		if (dest_class->valuetype || dest_class->enumtype || src_class->valuetype || src_class->enumtype)
			return FALSE;

		if (mono_class_is_subclass_of (src_class, dest_class, FALSE))
			;
		/* Case2: object[] -> reftype[] (ArrayList::ToArray) */
		else if (mono_class_is_subclass_of (dest_class, src_class, FALSE)) {
			// FIXME: This is racy
			return FALSE;
			/*
			  int i;
			for (i = source_idx; i < source_idx + length; ++i) {
				MonoObject *elem = mono_array_get (source, MonoObject*, i);
				if (elem && !mono_object_isinst (elem, dest_class))
					return FALSE;
			}
			*/
		} else
			return FALSE;
	}

	if (dest_class->valuetype) {
		element_size = mono_array_element_size (source->obj.vtable->klass);
		source_addr = mono_array_addr_with_size (source, element_size, source_idx);
		if (dest_class->has_references) {
			mono_value_copy_array (dest, dest_idx, source_addr, length);
		} else {
			dest_addr = mono_array_addr_with_size (dest, element_size, dest_idx);
			memmove (dest_addr, source_addr, element_size * length);
		}
	} else {
		mono_array_memcpy_refs (dest, dest_idx, source, source_idx, length);
	}

	return TRUE;
}

newest version:
ICALL_EXPORT gboolean
ves_icall_System_Array_FastCopy (MonoArray *source, int source_idx, MonoArray* dest, int dest_idx, int length)
{
	int element_size;
	void * dest_addr;
	void * source_addr;
	MonoVTable *src_vtable;
	MonoVTable *dest_vtable;
	MonoClass *src_class;
	MonoClass *dest_class;

	src_vtable = source->obj.vtable;
	dest_vtable = dest->obj.vtable;

	if (src_vtable->rank != dest_vtable->rank)
		return FALSE;

	if (source->bounds || dest->bounds)
		return FALSE;

	/* there's no integer overflow since mono_array_length returns an unsigned integer */
	if ((dest_idx + length > mono_array_length_fast (dest)) ||
		(source_idx + length > mono_array_length_fast (source)))
		return FALSE;

	src_class = src_vtable->klass->element_class;
	dest_class = dest_vtable->klass->element_class;

	/*
	 * Handle common cases.
	 */

	/* Case1: object[] -> valuetype[] (ArrayList::ToArray) 
	We fallback to managed here since we need to typecheck each boxed valuetype before storing them in the dest array.
	*/
	if (src_class == mono_defaults.object_class && dest_class->valuetype)
		return FALSE;

	/* Check if we're copying a char[] <==> (u)short[] */
	if (src_class != dest_class) {
		if (dest_class->valuetype || dest_class->enumtype || src_class->valuetype || src_class->enumtype)
			return FALSE;

		/* It's only safe to copy between arrays if we can ensure the source will always have a subtype of the destination. We bail otherwise. */
		if (!mono_class_is_subclass_of (src_class, dest_class, FALSE))
			return FALSE;
	}

	if (dest_class->valuetype) {
		element_size = mono_array_element_size (source->obj.vtable->klass);
		source_addr = mono_array_addr_with_size_fast (source, element_size, source_idx);
		if (dest_class->has_references) {
			mono_value_copy_array (dest, dest_idx, source_addr, length);
		} else {
			dest_addr = mono_array_addr_with_size_fast (dest, element_size, dest_idx);
			mono_gc_memmove_atomic (dest_addr, source_addr, element_size * length);
		}
	} else {
		mono_array_memcpy_refs_fast (dest, dest_idx, source, source_idx, length);
	}

	return TRUE;
}
