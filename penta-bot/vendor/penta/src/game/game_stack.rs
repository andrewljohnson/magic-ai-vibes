// Ordered stack-zone storage, kept apart from the game state that holds it:
// what belongs here is the order, and nothing about what the objects mean.
// Included textually into `mod.rs`, so the imports here are that module's.

/// Ordered stack-zone storage. The top is the final element, while removal by
/// index permits effects such as a counterspell to remove any targeted stack
/// object without disturbing the relative order of objects above or below it.
/// Stack order is therefore structural and never inferred from object IDs.
#[derive(Clone, Debug, Default)]
struct GameStack {
    objects: Vec<StackObject>,
}

impl GameStack {
    fn push(&mut self, object: StackObject) {
        self.objects.push(object);
    }

    fn pop(&mut self) -> Option<StackObject> {
        self.objects.pop()
    }

    fn remove(&mut self, index: usize) -> StackObject {
        self.objects.remove(index)
    }

    #[cfg(test)]
    fn clear(&mut self) {
        self.objects.clear();
    }

    fn is_empty(&self) -> bool {
        self.objects.is_empty()
    }

    #[cfg(test)]
    fn len(&self) -> usize {
        self.objects.len()
    }

    #[cfg(test)]
    fn last(&self) -> Option<&StackObject> {
        self.objects.last()
    }

    fn iter(&self) -> std::slice::Iter<'_, StackObject> {
        self.objects.iter()
    }

    fn iter_mut(&mut self) -> std::slice::IterMut<'_, StackObject> {
        self.objects.iter_mut()
    }
}

impl std::ops::Index<usize> for GameStack {
    type Output = StackObject;

    fn index(&self, index: usize) -> &Self::Output {
        &self.objects[index]
    }
}

impl std::ops::IndexMut<usize> for GameStack {
    fn index_mut(&mut self, index: usize) -> &mut Self::Output {
        &mut self.objects[index]
    }
}
