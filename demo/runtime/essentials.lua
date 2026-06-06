-- runtime/essentials.lua
-- nothing special, just simple lua wrappers

function clamp(val, min, max)
    return math.max(min, math.min(max, val))
end

function sin(x)
    return math.sin(x)
end

function cos(x)
    return math.cos(x)
end

function tan(x)
	return math.tan(x)
end

function flr(x)
	return math.floor(x)
end

function ceil(x)
	return math.ceil(x)
end

function min(x, num)
	return math.min(x, num)
end

function max(x)
	return math.max(x)
end

function mid(x, y, z)
    return math.max(math.min(x, y), math.min(math.max(x, y), z))
end

function sqrt(x)
	return math.sqrt(x)
end

function add(table, object)
	return table.insert(table, object)
end

function t()
    return os.clock()
end
